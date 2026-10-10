#include "IspProgrammer.h"

namespace
{
const uint8_t kOk = 0x10;
const uint8_t kFailed = 0x11;
const uint8_t kUnknown = 0x12;
const uint8_t kInsync = 0x14;
const uint8_t kNosync = 0x15;
const uint8_t kEop = 0x20;
const uint8_t kGetSync = 0x30;
const uint8_t kGetSignOn = 0x31;
const uint8_t kSetParameter = 0x40;
const uint8_t kGetParameter = 0x41;
const uint8_t kSetDevice = 0x42;
const uint8_t kSetDeviceExt = 0x45;
const uint8_t kEnterProgmode = 0x50;
const uint8_t kLeaveProgmode = 0x51;
const uint8_t kChipErase = 0x52;
const uint8_t kCheckAutoinc = 0x53;
const uint8_t kLoadAddress = 0x55;
const uint8_t kUniversal = 0x56;
const uint8_t kProgFlash = 0x60;
const uint8_t kProgData = 0x61;
const uint8_t kProgPage = 0x64;
const uint8_t kReadPage = 0x74;
const uint8_t kReadSign = 0x75;
const char kSignOn[] = "AVR ISP";
}

// ========== Construction ==========

IspProgrammer::IspProgrammer(IBytePort& port, ISpiMaster& spi,
                             IDigitalPin& reset, IClock& clock)
  : _port(port), _spi(spi), _reset(&reset), _clock(clock)
{
}

/**
 * Binds the reset pin and drives it high. The GPIO number is unused
 * because SPI pins are not per slot. Does not start SPI and does not
 * drive the pin low.
 *
 * @param line Reset pin for this session.
 * @param gpio Unused.
 * @return Nothing.
 */
void IspProgrammer::start(IDigitalPin& line, uint8_t gpio)
{
  (void)gpio;
  _reset = &line;
  _reset->setMode(PinMode::DigitalOutput);
  _reset->write(true);
}


// ========== Public API ==========

/**
 * Consumes queued STK500 bytes and continues any timed SPI step.
 *
 * @return Nothing.
 */
void IspProgrammer::update()
{
  if (_waiting)
  {
    if (!_waitElapsed())
    {
      return;
    }
    _waiting = false;
    const AfterWait next = _afterWait;
    _afterWait = AfterWait::None;
    _continue(next);
  }

  while (_port.available() > 0 && !_waiting)
  {
    const int value = _port.read();
    if (value < 0)
    {
      break;
    }
    _activity++;
    _accept(static_cast<uint8_t>(value));
  }
}

/**
 * Releases SPI and returns reset to an input with pull-up.
 *
 * @return Nothing.
 */
void IspProgrammer::shutdown()
{
  if (_spiActive)
  {
    _spi.end();
    _spiActive = false;
  }
  _reset->setMode(PinMode::DigitalInputPullup);
  _programming = false;
  _waiting = false;
  _afterWait = AfterWait::None;
  _phase = Phase::Command;
}

/**
 * Counts bytes accepted from the port.
 *
 * @return Monotonic count of accepted bytes.
 */
uint32_t IspProgrammer::activityCount() const
{
  return _activity;
}


// ========== Parsing ==========

/**
 * Accepts one STK500 byte.
 *
 * @param byte Next protocol byte.
 * @return Nothing.
 */
void IspProgrammer::_accept(uint8_t byte)
{
  if (_phase == Phase::Command)
  {
    _cmd = byte;
    _fixedCount = 0;
    _headerCount = 0;
    _pageCount = 0;
    _oversize = false;
    if (byte == kEop)
    {
      _replyNosync();
      return;
    }
    if (byte == kProgPage || byte == kReadPage)
    {
      _pageIsRead = byte == kReadPage;
      _phase = Phase::PageHeader;
      return;
    }
    const int payload = _payloadLen(byte);
    if (payload < 0)
    {
      _phase = Phase::UnknownEop;
      return;
    }
    _fixedNeed = static_cast<uint8_t>(payload + 1);
    _phase = Phase::Fixed;
    return;
  }

  if (_phase == Phase::UnknownEop)
  {
    _phase = Phase::Command;
    if (byte == kEop)
    {
      _write(kUnknown);
    }
    else
    {
      _replyNosync();
    }
    return;
  }

  if (_phase == Phase::Fixed)
  {
    _fixed[_fixedCount++] = byte;
    if (_fixedCount < _fixedNeed)
    {
      return;
    }
    _phase = Phase::Command;
    if (_fixed[_fixedNeed - 1] != kEop)
    {
      _replyNosync();
      return;
    }
    _dispatchFixed();
    return;
  }

  if (_phase == Phase::PageHeader)
  {
    _header[_headerCount++] = byte;
    if (_headerCount < 3)
    {
      return;
    }
    _pageLen = static_cast<uint16_t>((_header[0] << 8) | _header[1]);
    _memType = _header[2];
    if (_pageLen > kMaxPageBytes)
    {
      _oversize = true;
      if (_pageIsRead)
      {
        _phase = Phase::Eop;
        return;
      }
      _discardLeft = _pageLen;
      _phase = Phase::Discard;
      return;
    }
    if (_pageIsRead || _pageLen == 0)
    {
      _phase = Phase::Eop;
      return;
    }
    _phase = Phase::PageBody;
    return;
  }

  if (_phase == Phase::PageBody)
  {
    _page[_pageCount++] = byte;
    if (_pageCount < _pageLen)
    {
      return;
    }
    _phase = Phase::Eop;
    return;
  }

  if (_phase == Phase::Discard)
  {
    if (_discardLeft > 0)
    {
      _discardLeft--;
    }
    if (_discardLeft == 0)
    {
      _phase = Phase::Eop;
    }
    return;
  }

  _phase = Phase::Command;
  if (byte != kEop)
  {
    _replyNosync();
    return;
  }
  _dispatchPage();
}

/**
 * Payload bytes before the end-of-packet marker, or -1 when unknown.
 *
 * @param cmd Command byte.
 * @return Payload length, or -1.
 */
int IspProgrammer::_payloadLen(uint8_t cmd) const
{
  switch (cmd)
  {
    case kGetSync:
    case kGetSignOn:
    case kEnterProgmode:
    case kLeaveProgmode:
    case kChipErase:
    case kCheckAutoinc:
    case kReadSign:
      return 0;
    case kGetParameter:
    case kProgData:
      return 1;
    case kSetParameter:
    case kLoadAddress:
    case kProgFlash:
      return 2;
    case kUniversal:
      return 4;
    case kSetDeviceExt:
      return 5;
    case kSetDevice:
      return 20;
    default:
      return -1;
  }
}

/**
 * Runs a command whose payload is already buffered.
 *
 * @return Nothing.
 */
void IspProgrammer::_dispatchFixed()
{
  if (_cmd == kGetSync || _cmd == kCheckAutoinc || _cmd == kProgFlash ||
      _cmd == kProgData || _cmd == kSetDeviceExt)
  {
    _replyOk();
    return;
  }

  if (_cmd == kGetSignOn)
  {
    uint8_t reply[2 + sizeof(kSignOn) - 1];
    reply[0] = kInsync;
    for (size_t i = 0; i < sizeof(kSignOn) - 1; ++i)
    {
      reply[i + 1] = static_cast<uint8_t>(kSignOn[i]);
    }
    reply[sizeof(reply) - 1] = kOk;
    _port.write(reply, sizeof(reply));
    return;
  }

  if (_cmd == kSetParameter)
  {
    _parameters[_fixed[0]] = _fixed[1];
    _replyOk();
    return;
  }

  if (_cmd == kGetParameter)
  {
    const uint8_t which = _fixed[0];
    uint8_t value = _parameters[which];
    if (which == 0x80)
    {
      value = 2;
    }
    else if (which == 0x81)
    {
      value = 1;
    }
    else if (which == 0x82)
    {
      value = 18;
    }
    else if (which == 0x93)
    {
      value = static_cast<uint8_t>('S');
    }
    const uint8_t reply[] = {kInsync, value, kOk};
    _port.write(reply, sizeof(reply));
    return;
  }

  if (_cmd == kSetDevice)
  {
    _pageSizeBytes = static_cast<uint16_t>((_fixed[12] << 8) | _fixed[13]);
    _replyOk();
    return;
  }

  if (_cmd == kLoadAddress)
  {
    _address = static_cast<uint16_t>(_fixed[0] | (_fixed[1] << 8));
    _replyOk();
    return;
  }

  if (_cmd == kEnterProgmode)
  {
    _beginProgramming();
    return;
  }

  if (_cmd == kLeaveProgmode)
  {
    if (_spiActive)
    {
      _spi.end();
      _spiActive = false;
    }
    _reset->setMode(PinMode::DigitalOutput);
    _reset->write(true);
    _programming = false;
    _replyOk();
    return;
  }

  if (_cmd == kChipErase)
  {
    _transfer4(0xAC, 0x80, 0x00, 0x00, nullptr);
    _defer(AfterWait::ReplyOk, kChipEraseMs);
    return;
  }

  if (_cmd == kUniversal)
  {
    uint8_t rx[4] = {};
    _transfer4(_fixed[0], _fixed[1], _fixed[2], _fixed[3], rx);
    if (_fixed[0] == 0xAC && _fixed[1] == 0x80 && _fixed[2] == 0x00 &&
        _fixed[3] == 0x00)
    {
      _deferredByte = rx[3];
      _defer(AfterWait::ReplyEraseByte, kChipEraseMs);
      return;
    }
    const uint8_t reply[] = {kInsync, rx[3], kOk};
    _port.write(reply, sizeof(reply));
    return;
  }

  if (_cmd == kReadSign)
  {
    _write(kInsync);
    for (uint8_t index = 0; index < 3; ++index)
    {
      uint8_t rx[4] = {};
      _transfer4(0x30, 0x00, index, 0x00, rx);
      _write(rx[3]);
    }
    _write(kOk);
    return;
  }

  _replyOk();
}

/**
 * Runs a paged read or write after its payload and end marker.
 *
 * @return Nothing.
 */
void IspProgrammer::_dispatchPage()
{
  if (_oversize || (_memType != 'F' && _memType != 'E'))
  {
    _replyFailed();
    return;
  }
  if (_memType == 'F' && (_pageLen % 2) != 0)
  {
    _replyFailed();
    return;
  }
  if (_pageIsRead)
  {
    _readPage();
    return;
  }
  if (_pageLen == 0)
  {
    _replyOk();
    return;
  }
  if (_memType == 'F')
  {
    _programFlash();
    return;
  }
  _programEeprom();
}


// ========== ISP commands ==========

/**
 * Starts programming mode: SPI first, then reset low.
 *
 * @return Nothing.
 */
void IspProgrammer::_beginProgramming()
{
  if (_programming)
  {
    _replyOk();
    return;
  }
  if (!_spiActive)
  {
    _spi.begin(kSpiClockHz);
    _spiActive = true;
  }
  _reset->setMode(PinMode::DigitalOutput);
  _reset->write(false);
  _enableAttempts = 0;
  _defer(AfterWait::ProgrammingEnable, kResetSettleMs);
}

/**
 * Sends programming enable, or pulses reset and waits to retry.
 *
 * @return Nothing.
 */
void IspProgrammer::_tryProgrammingEnable()
{
  uint8_t rx[4] = {};
  _transfer4(0xAC, 0x53, 0x00, 0x00, rx);
  if (rx[2] == 0x53)
  {
    _programming = true;
    _replyOk();
    return;
  }
  _enableAttempts++;
  if (_enableAttempts >= 3)
  {
    _reset->write(true);
    _programming = false;
    _replyFailed();
    return;
  }
  _reset->write(true);
  _reset->write(false);
  _defer(AfterWait::ProgrammingEnable, kResetSettleMs);
}

/**
 * Loads a flash page and arms the page-write wait.
 *
 * @return Nothing.
 */
void IspProgrammer::_programFlash()
{
  const uint16_t start = _address;
  for (uint16_t index = 0; index < _pageLen;
       index = static_cast<uint16_t>(index + 2))
  {
    _transfer4(0x40, static_cast<uint8_t>(_address >> 8),
               static_cast<uint8_t>(_address & 0xFF), _page[index], nullptr);
    _transfer4(0x48, static_cast<uint8_t>(_address >> 8),
               static_cast<uint8_t>(_address & 0xFF), _page[index + 1],
               nullptr);
    _address++;
  }
  uint16_t commitAt = start;
  if (_pageSizeBytes >= 2)
  {
    const uint16_t words =
        static_cast<uint16_t>(_pageSizeBytes / 2);
    commitAt = static_cast<uint16_t>(start & ~(words - 1));
  }
  _transfer4(0x4C, static_cast<uint8_t>(commitAt >> 8),
             static_cast<uint8_t>(commitAt & 0xFF), 0x00, nullptr);
  _defer(AfterWait::ReplyOk, kFlashPageWriteMs);
}

/**
 * Starts a byte-by-byte EEPROM write.
 *
 * @return Nothing.
 */
void IspProgrammer::_programEeprom()
{
  _eepromAddress = static_cast<uint16_t>(_address * 2);
  _eepromIndex = 0;
  _programNextEepromByte();
}

/**
 * Writes the next EEPROM byte, or replies when the buffer is done.
 *
 * @return Nothing.
 */
void IspProgrammer::_programNextEepromByte()
{
  if (_eepromIndex >= _pageLen)
  {
    _replyOk();
    return;
  }
  const uint16_t addr =
      static_cast<uint16_t>(_eepromAddress + _eepromIndex);
  _transfer4(0xC0, static_cast<uint8_t>(addr >> 8),
             static_cast<uint8_t>(addr & 0xFF), _page[_eepromIndex], nullptr);
  _eepromIndex++;
  _defer(AfterWait::EepromNext, kEepromWriteMs);
}

/**
 * Reads one flash or EEPROM page back to the port.
 *
 * @return Nothing.
 */
void IspProgrammer::_readPage()
{
  _write(kInsync);
  if (_memType == 'F')
  {
    for (uint16_t index = 0; index < _pageLen;
         index = static_cast<uint16_t>(index + 2))
    {
      uint8_t rx[4] = {};
      _transfer4(0x20, static_cast<uint8_t>(_address >> 8),
                 static_cast<uint8_t>(_address & 0xFF), 0x00, rx);
      _write(rx[3]);
      _transfer4(0x28, static_cast<uint8_t>(_address >> 8),
                 static_cast<uint8_t>(_address & 0xFF), 0x00, rx);
      _write(rx[3]);
      _address++;
    }
  }
  else
  {
    uint16_t addr = static_cast<uint16_t>(_address * 2);
    for (uint16_t index = 0; index < _pageLen; ++index)
    {
      uint8_t rx[4] = {};
      _transfer4(0xA0, static_cast<uint8_t>(addr >> 8),
                 static_cast<uint8_t>(addr & 0xFF), 0xFF, rx);
      _write(rx[3]);
      addr++;
    }
  }
  _write(kOk);
}

/**
 * Clocks a four-byte ISP instruction.
 *
 * @param a First byte.
 * @param b Second byte.
 * @param c Third byte.
 * @param d Fourth byte.
 * @param rx Optional four response bytes.
 * @return Nothing.
 */
void IspProgrammer::_transfer4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                               uint8_t* rx)
{
  const uint8_t r0 = _spi.transfer(a);
  const uint8_t r1 = _spi.transfer(b);
  const uint8_t r2 = _spi.transfer(c);
  const uint8_t r3 = _spi.transfer(d);
  if (rx != nullptr)
  {
    rx[0] = r0;
    rx[1] = r1;
    rx[2] = r2;
    rx[3] = r3;
  }
}


// ========== Timing and replies ==========

/**
 * Arms a wait and stops reading until it elapses.
 *
 * @param next Step to run when the wait elapses.
 * @param milliseconds Wait length.
 * @return Nothing.
 */
void IspProgrammer::_defer(AfterWait next, uint32_t milliseconds)
{
  _afterWait = next;
  _waitMs = milliseconds;
  _waitStartedAt = _clock.millis();
  _waiting = true;
}

/**
 * Runs the step armed by _defer.
 *
 * @param next Step to run.
 * @return Nothing.
 */
void IspProgrammer::_continue(AfterWait next)
{
  if (next == AfterWait::ProgrammingEnable)
  {
    _tryProgrammingEnable();
    return;
  }
  if (next == AfterWait::ReplyOk)
  {
    _replyOk();
    return;
  }
  if (next == AfterWait::ReplyEraseByte)
  {
    const uint8_t reply[] = {kInsync, _deferredByte, kOk};
    _port.write(reply, sizeof(reply));
    return;
  }
  if (next == AfterWait::EepromNext)
  {
    _programNextEepromByte();
  }
}

/**
 * Reports whether the armed wait has elapsed.
 *
 * @return True when the wait is over.
 */
bool IspProgrammer::_waitElapsed() const
{
  return static_cast<uint32_t>(_clock.millis() - _waitStartedAt) >= _waitMs;
}

/**
 * Writes one response byte.
 *
 * @param byte Response byte.
 * @return Nothing.
 */
void IspProgrammer::_write(uint8_t byte)
{
  _port.write(&byte, 1);
}

/**
 * Writes in-sync and OK.
 *
 * @return Nothing.
 */
void IspProgrammer::_replyOk()
{
  const uint8_t reply[] = {kInsync, kOk};
  _port.write(reply, sizeof(reply));
}

/**
 * Writes in-sync and failed.
 *
 * @return Nothing.
 */
void IspProgrammer::_replyFailed()
{
  const uint8_t reply[] = {kInsync, kFailed};
  _port.write(reply, sizeof(reply));
}

/**
 * Writes not-in-sync.
 *
 * @return Nothing.
 */
void IspProgrammer::_replyNosync()
{
  _write(kNosync);
}

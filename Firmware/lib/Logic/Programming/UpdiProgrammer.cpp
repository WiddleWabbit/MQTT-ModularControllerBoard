#include "UpdiProgrammer.h"

namespace
{
const uint8_t kStart = 0x1B;
const uint8_t kToken = 0x0E;
const uint8_t kSynch = 0x55;
const uint8_t kAck = 0x40;

const uint8_t kRspOk = 0x80;
const uint8_t kRspParameter = 0x81;
const uint8_t kRspMemory = 0x82;
const uint8_t kRspSignOn = 0x86;
const uint8_t kRspFailed = 0xA0;
const uint8_t kRspIllegalParameter = 0xA1;
const uint8_t kRspIllegalMemory = 0xA2;
const uint8_t kRspIllegalState = 0xA5;
const uint8_t kRspIllegalCommand = 0xAA;
const uint8_t kRspNoPower = 0xAB;

const uint8_t kCmdSignOff = 0x00;
const uint8_t kCmdSignOn = 0x01;
const uint8_t kCmdSetParameter = 0x02;
const uint8_t kCmdGetParameter = 0x03;
const uint8_t kCmdWriteMemory = 0x04;
const uint8_t kCmdReadMemory = 0x05;
const uint8_t kCmdGo = 0x08;
const uint8_t kCmdReset = 0x0B;
const uint8_t kCmdSetDescriptor = 0x0C;
const uint8_t kCmdGetSync = 0x0F;
const uint8_t kCmdEnter = 0x14;
const uint8_t kCmdLeave = 0x15;
const uint8_t kCmdErase = 0x34;

const uint8_t kParHw = 0x01;
const uint8_t kParFw = 0x02;
const uint8_t kParEmu = 0x03;
const uint8_t kParBaud = 0x05;
const uint8_t kParVtarget = 0x06;

const uint8_t kMemFlash = 0xC0;
const uint8_t kMemBoot = 0xC1;
const uint8_t kMemEeprom = 0x22;
const uint8_t kMemEepromXmega = 0xC4;
const uint8_t kMemUsersig = 0xC5;
const uint8_t kMemFuse = 0xB2;
const uint8_t kMemLock = 0xB3;

const uint8_t kEraseChip = 0;
const uint8_t kEraseAppPage = 4;
const uint8_t kEraseBootPage = 5;
const uint8_t kEraseEepromPage = 6;
const uint8_t kEraseUsersig = 7;

const uint8_t kRegStatusA = 0;
const uint8_t kRegControlA = 2;
const uint8_t kRegControlB = 3;
const uint8_t kRegKeyStatus = 7;
const uint8_t kRegReset = 8;
const uint8_t kRegSysStatus = 11;

const uint16_t kNvmBase = 0x1000;
const uint16_t kNvmStatus = 0x1002;
const uint16_t kNvmData = 0x1006;
const uint8_t kNvmWp = 1;
const uint8_t kNvmEr = 2;
const uint8_t kNvmErwp = 3;
const uint8_t kNvmPbc = 4;
const uint8_t kNvmWfu = 7;

const uint8_t kResetOn = 0x59;

// Sixteen bit times. Stored only after enter. The post-break store
// stays 0x00. Two bit times (0x06) lets the target pulse before the
// pull-up is off, so that value is not used.
const uint8_t kGuardAfterEnter = 0x03;

// Five times the 4 ms typical page erase-write. A stuck busy bit
// must still answer inside avrdude's 200 ms write window.
const uint32_t kNvmWaitMs = 20;

const uint8_t kNvmKey[8] = {0x20, 0x67, 0x6F, 0x72, 0x50, 0x4D, 0x56, 0x4E};
const uint8_t kEraseKey[8] = {0x65, 0x73, 0x61, 0x72, 0x45, 0x4D, 0x56, 0x4E};

const uint8_t kSignOn[29] = {
  kRspSignOn, 0x01,
  0x01, 0x00, 0x06, 0x01,
  0x01, 0x00, 0x06, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  'J', 'T', 'A', 'G', 'I', 'C', 'E', ' ', 'm', 'k', 'I', 'I', 0};

/**
 * AVR067 CRC-16 used by avrdude and jtag2updi. Init 0xFFFF, reflected
 * polynomial 0x8408. The check value of "123456789" is 0x6F91.
 *
 * @param data Bytes from the start marker through the last body byte.
 * @param length Number of bytes.
 * @return CRC value. Frames store it little-endian.
 */
uint16_t crc16Jtag(const uint8_t* data, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index)
  {
    crc = static_cast<uint16_t>(crc ^ data[index]);
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if ((crc & 0x0001) != 0)
      {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408);
      }
      else
      {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}
}


// ========== Construction ==========

UpdiProgrammer::UpdiProgrammer(IBytePort& port, IHalfDuplexUart& uart,
                               IClock& clock)
  : _port(port), _uart(uart), _clock(clock)
{
}


// ========== Public API ==========

/**
 * Stores the UPDI pin and leaves it as an input with a pull-up.
 * The UART stays detached until a connect break finishes. The wire
 * has no board pull-up, so this pull-up holds it high.
 *
 * @param line Pin used as the UPDI wire.
 * @param gpio GPIO number of that pin.
 * @return Nothing.
 */
void UpdiProgrammer::start(IDigitalPin& line, uint8_t gpio)
{
  _line = &line;
  _gpio = gpio;
  _line->setMode(PinMode::DigitalInputPullup);
}

/**
 * Advances one break phase, then reads host bytes until a new break
 * starts or the port is empty. A link check leaves those bytes unread.
 *
 * @return Nothing.
 */
void UpdiProgrammer::update()
{
  if (_checkRunning)
  {
    return;
  }
  if (_breakPhase != BreakPhase::Idle)
  {
    if (!_advanceBreak())
    {
      return;
    }
    _runPending();
  }

  while (_port.available() > 0 && _breakPhase == BreakPhase::Idle)
  {
    const int value = _port.read();
    if (value < 0)
    {
      break;
    }
    _activity++;
    _acceptByte(static_cast<uint8_t>(value));
  }
}

/**
 * Detaches the UART and returns the line to an input with no pull-up.
 *
 * @return Nothing.
 */
void UpdiProgrammer::shutdown()
{
  if (_uartInstalled)
  {
    _uart.detach();
    _uartInstalled = false;
    _uartAttached = false;
  }
  if (_line != nullptr)
  {
    _line->setMode(PinMode::DigitalInput);
  }
  _breakPhase = BreakPhase::Idle;
  _pending = Pending::None;
  _connected = false;
  _inProg = false;
  _linkOk = false;
  _checkRunning = false;
  _rx = Rx::Start;
  _crcCount = 0;
}

/**
 * Installs the UART with the pad still a GPIO, then drives the
 * open-drain low that starts the double break. The driven level is
 * stored for the report. Host bytes stay unread. Later polls advance
 * the phases, so this call does not release the pin.
 *
 * @param line Pin used as the UPDI wire.
 * @param gpio GPIO number of that pin.
 * @return Nothing.
 */
void UpdiProgrammer::beginLinkCheck(IDigitalPin& line, uint8_t gpio)
{
  _line = &line;
  _gpio = gpio;
  _checkResult.drivenHigh = false;
  _checkResult.releasedHigh = false;
  _checkResult.replied = false;
  _checkResult.echoSeen = false;
  _checkResult.listenLow = false;
  _checkResult.pulseAtUs = 0;
  _checkResult.pulseUs = 0;
  _checkResult.edgeCount = 0;
  for (uint8_t index = 0; index < kUartListenWidths; ++index)
  {
    _checkResult.widthUs[index] = 0;
  }
  _checkResult.uartFault = UartListenFault::None;
  _checkResult.status = 0;
  _checkRunning = true;
  _pending = Pending::None;
  _connected = false;
  if (_uartInstalled)
  {
    _uart.detach();
    _uartInstalled = false;
    _uartAttached = false;
  }
  _uart.install(_gpio, kBaud);
  _uartInstalled = true;
  _uartAttached = false;
  _line->setMode(PinMode::DigitalOutputOpenDrain);
  _line->write(false);
  _checkResult.drivenHigh = _line->read();
  _breakMark = _clock.millis();
  _breakPhase = BreakPhase::Low1;
}

/**
 * Advances one phase of the double break. The call that finishes the
 * second low claims the pad, loads STATUSA, and restores the pin.
 * Host bytes stay unread.
 *
 * @param result Receives the measurement when this call finishes it.
 * @return True once, after the pin is restored.
 */
bool UpdiProgrammer::pollLinkCheck(UpdiLinkCheck& result)
{
  if (!_checkRunning)
  {
    return false;
  }
  if (!_advanceBreak())
  {
    return false;
  }
  _finishLinkCheck();
  result = _checkResult;
  return true;
}

/**
 * Counts bytes accepted from the host port.
 *
 * @return Monotonic count of accepted bytes.
 */
uint32_t UpdiProgrammer::activityCount() const
{
  return _activity;
}


// ========== Framing ==========

/**
 * Accepts one host byte. A bad token or an oversized body resyncs
 * without a reply. A finished frame is handled immediately.
 *
 * @param byte Next protocol byte.
 * @return Nothing.
 */
void UpdiProgrammer::_acceptByte(uint8_t byte)
{
  switch (_rx)
  {
    case Rx::Start:
      if (byte == kStart)
      {
        _raw[0] = byte;
        _rawCount = 1;
        _rx = Rx::Seq;
      }
      break;
    case Rx::Seq:
      _raw[_rawCount++] = byte;
      if (_rawCount == 3)
      {
        _rx = Rx::Size;
      }
      break;
    case Rx::Size:
      _raw[_rawCount++] = byte;
      if (_rawCount == 7)
      {
        _bodyLen = static_cast<uint32_t>(_raw[3]) |
                   (static_cast<uint32_t>(_raw[4]) << 8) |
                   (static_cast<uint32_t>(_raw[5]) << 16) |
                   (static_cast<uint32_t>(_raw[6]) << 24);
        if (_bodyLen > kMaxBody)
        {
          _rx = Rx::Start;
          break;
        }
        _rx = Rx::Token;
      }
      break;
    case Rx::Token:
      if (byte != kToken)
      {
        _rx = Rx::Start;
        if (byte == kStart)
        {
          _acceptByte(byte);
        }
        break;
      }
      _raw[_rawCount++] = byte;
      _crcCount = 0;
      _rx = _bodyLen == 0 ? Rx::Crc : Rx::Body;
      break;
    case Rx::Body:
      _raw[_rawCount++] = byte;
      if (_rawCount == 8u + _bodyLen)
      {
        _crcCount = 0;
        _rx = Rx::Crc;
      }
      break;
    case Rx::Crc:
      _crcBytes[_crcCount++] = byte;
      if (_crcCount == 2)
      {
        _crcCount = 0;
        _rx = Rx::Start;
        _onFrame();
      }
      break;
  }
}

/**
 * Checks the AVR067 CRC over the start marker through the body. A mismatch
 * replies failed and leaves the programmer in the session.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_onFrame()
{
  _sequence = static_cast<uint16_t>(_raw[1] | (_raw[2] << 8));
  const uint16_t expect = crc16Jtag(_raw, 8u + _bodyLen);
  const uint16_t got = static_cast<uint16_t>(
    _crcBytes[0] | (_crcBytes[1] << 8));
  if (expect != got || _bodyLen == 0)
  {
    _replyStatus(kRspFailed);
    return;
  }
  _dispatch();
}

/**
 * Runs one command. Wire commands that need a connect break return
 * before any further host byte is read.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_dispatch()
{
  const uint8_t command = _body()[0];
  switch (command)
  {
    case kCmdSignOn:
      _emuMode = 0x02;
      _baudCode = 4;
      _reply(kSignOn, sizeof(kSignOn));
      return;
    case kCmdGetSync:
    case kCmdReset:
      _replyOk();
      return;
    case kCmdSignOff:
      if (_connected)
      {
        _linkOk = _uartAttached;
        if (_inProg)
        {
          _leaveTarget();
          _disableUpdi();
        }
        else
        {
          _connected = false;
        }
      }
      _baudCode = 4;
      if (_connected)
      {
        _replyNoPower();
      }
      else
      {
        _replyOk();
      }
      return;
    case kCmdSetParameter:
      if (_bodyLen < 3)
      {
        _replyStatus(kRspIllegalParameter);
        return;
      }
      if (_body()[1] == kParEmu)
      {
        _emuMode = _body()[2];
        _replyOk();
        return;
      }
      if (_body()[1] == kParBaud && _body()[2] >= 1 && _body()[2] <= 28)
      {
        _baudCode = _body()[2];
        _replyOk();
        return;
      }
      _replyStatus(kRspIllegalParameter);
      return;
    case kCmdGetParameter:
      if (_bodyLen < 2)
      {
        _replyStatus(kRspIllegalParameter);
        return;
      }
      if (_body()[1] == kParHw)
      {
        const uint8_t body[] = {kRspParameter, 0x01, 0x01};
        _reply(body, sizeof(body));
        return;
      }
      if (_body()[1] == kParFw)
      {
        const uint8_t body[] = {kRspParameter, 0x00, 0x06, 0x00, 0x06};
        _reply(body, sizeof(body));
        return;
      }
      if (_body()[1] == kParEmu)
      {
        const uint8_t body[] = {kRspParameter, _emuMode};
        _reply(body, sizeof(body));
        return;
      }
      if (_body()[1] == kParBaud)
      {
        const uint8_t body[] = {kRspParameter, _baudCode};
        _reply(body, sizeof(body));
        return;
      }
      if (_body()[1] == kParVtarget)
      {
        const uint8_t body[] = {kRspParameter, 0xE4, 0x0C};
        _reply(body, sizeof(body));
        return;
      }
      _replyStatus(kRspIllegalParameter);
      return;
    case kCmdSetDescriptor:
      _setDescriptor();
      return;
    case kCmdEnter:
      _beginEnter();
      return;
    case kCmdLeave:
      _linkOk = _uartAttached;
      _leaveProgmode();
      return;
    case kCmdGo:
      if (_connected && _inProg)
      {
        _linkOk = _uartAttached;
        if (_disableUpdi())
        {
          _replyOk();
        }
        else
        {
          _replyNoPower();
        }
        return;
      }
      _replyOk();
      return;
    case kCmdErase:
      _beginErase();
      return;
    case kCmdReadMemory:
      _linkOk = _uartAttached;
      _readMemory();
      return;
    case kCmdWriteMemory:
      _linkOk = _uartAttached;
      _writeMemory();
      return;
    default:
      _replyStatus(kRspIllegalCommand);
      return;
  }
}

/**
 * Points at the frame body, which starts after the token.
 *
 * @return Body bytes.
 */
const uint8_t* UpdiProgrammer::_body() const
{
  return &_raw[8];
}

/**
 * Writes a one-byte status answer.
 *
 * @param status jtagice response code.
 * @return Nothing.
 */
void UpdiProgrammer::_replyStatus(uint8_t status)
{
  _reply(&status, 1);
}

/**
 * Writes a status byte plus one parameter.
 *
 * @param status jtagice response code.
 * @param param Extra byte.
 * @return Nothing.
 */
void UpdiProgrammer::_replyStatusParam(uint8_t status, uint8_t param)
{
  const uint8_t body[] = {status, param};
  _reply(body, sizeof(body));
}

/**
 * Writes RSP_OK.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_replyOk()
{
  _replyStatus(kRspOk);
}

/**
 * Writes RSP_NO_TARGET_POWER.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_replyNoPower()
{
  _replyStatus(kRspNoPower);
}

/**
 * Writes one answer frame using the sequence number of the request.
 *
 * @param body Answer body.
 * @param length Body length.
 * @return Nothing.
 */
void UpdiProgrammer::_reply(const uint8_t* body, uint16_t length)
{
  uint8_t frame[8 + kMaxBody];
  if (length > kMaxBody)
  {
    length = static_cast<uint16_t>(kMaxBody);
  }
  frame[0] = kStart;
  frame[1] = static_cast<uint8_t>(_sequence & 0xFF);
  frame[2] = static_cast<uint8_t>(_sequence >> 8);
  frame[3] = static_cast<uint8_t>(length & 0xFF);
  frame[4] = static_cast<uint8_t>((length >> 8) & 0xFF);
  frame[5] = 0;
  frame[6] = 0;
  frame[7] = kToken;
  for (uint16_t index = 0; index < length; ++index)
  {
    frame[8 + index] = body[index];
  }
  const uint16_t crc = crc16Jtag(frame, 8u + length);
  _port.write(frame, 8u + length);
  const uint8_t crcBytes[] = {
    static_cast<uint8_t>(crc & 0xFF),
    static_cast<uint8_t>(crc >> 8)};
  _port.write(crcBytes, sizeof(crcBytes));
}


// ========== Break ==========

/**
 * Installs the UART, then holds the line low. The driver install
 * finishes before the break timer starts. One later update() moves
 * each of the three timed phases. The pad stays a GPIO until claim().
 *
 * @param pending Command to run when the line is released.
 * @return Nothing.
 */
void UpdiProgrammer::_startBreak(Pending pending)
{
  _pending = pending;
  if (_uartInstalled)
  {
    _uart.detach();
    _uartInstalled = false;
    _uartAttached = false;
  }
  _uart.install(_gpio, kBaud);
  _uartInstalled = true;
  _uartAttached = false;
  _connected = false;
  if (_line != nullptr)
  {
    _line->setMode(PinMode::DigitalOutputOpenDrain);
    _line->write(false);
  }
  _breakMark = _clock.millis();
  _breakPhase = BreakPhase::Low1;
}

/**
 * Moves 25 ms open-drain low, 3 ms input pull-up, then 25 ms
 * open-drain low. The pull-up is the high between the breaks. The
 * MOD net has no board pull-up, and open-drain high only releases
 * the driver. A link check samples the pin at the end of that pull-up.
 * The completing call releases the line, claims the pad, and returns
 * so the waiting instruction is written in that same call.
 *
 * @return True when this call finished the break.
 */
bool UpdiProgrammer::_advanceBreak()
{
  const uint32_t now = _clock.millis();
  const uint32_t elapsed = static_cast<uint32_t>(now - _breakMark);
  if (_breakPhase == BreakPhase::Low1)
  {
    if (elapsed < kBreakLowMs)
    {
      return false;
    }
    if (_line != nullptr)
    {
      _line->setMode(PinMode::DigitalInputPullup);
    }
    _breakMark = now;
    _breakPhase = BreakPhase::High;
    return false;
  }
  if (_breakPhase == BreakPhase::High)
  {
    if (elapsed < kBreakHighMs)
    {
      return false;
    }
    if (_checkRunning && _line != nullptr)
    {
      _checkResult.releasedHigh = _line->read();
    }
    if (_line != nullptr)
    {
      _line->setMode(PinMode::DigitalOutputOpenDrain);
    }
    _breakMark = now;
    _breakPhase = BreakPhase::Low2;
    return false;
  }
  if (elapsed < kBreakLowMs)
  {
    return false;
  }
  if (_line != nullptr)
  {
    _line->write(true);
  }
  _uart.claim();
  _uartAttached = true;
  _connected = true;
  _breakPhase = BreakPhase::Idle;
  return true;
}

/**
 * Runs the command that waited for the break, on the newly attached
 * UART.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_runPending()
{
  const Pending pending = _pending;
  _pending = Pending::None;
  _linkOk = _uartAttached;
  if (pending == Pending::Descriptor)
  {
    _finishDescriptor();
  }
  else if (pending == Pending::Enter)
  {
    _enterProgmode();
  }
  else if (pending == Pending::Erase)
  {
    _erase();
  }
}

/**
 * Reads UPDI STATUSA, then detaches the UART and restores the pin to
 * an input. The drive and release samples stay in _checkResult.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_finishLinkCheck()
{
  _linkOk = _uartAttached;
  _uart.clearListen();
  const uint8_t frame[] = {
    kSynch, static_cast<uint8_t>(0x80 + kRegStatusA)};
  _checkResult.echoSeen = _send(frame, sizeof(frame));
  if (!_checkResult.echoSeen)
  {
    _checkResult.replied = false;
    _checkResult.status = 0;
  }
  else
  {
    const int value = _get();
    if (value < 0)
    {
      _checkResult.replied = false;
      _checkResult.status = 0;
    }
    else
    {
      _checkResult.replied = true;
      _checkResult.status = static_cast<uint8_t>(value);
    }
  }
  _checkResult.listenLow = _uart.listenWentLow();
  _checkResult.pulseAtUs = _uart.listenPulseAtUs();
  _checkResult.pulseUs = _uart.listenPulseWidthUs();
  _checkResult.edgeCount = _uart.listenEdgeCount();
  for (uint8_t index = 0; index < kUartListenWidths; ++index)
  {
    _checkResult.widthUs[index] = _uart.listenWidthUs(index);
  }
  _checkResult.uartFault = _uart.listenFault();
  shutdown();
}

/**
 * Records page sizes from a descriptor of at least 247 bytes, then
 * always double-breaks, including when the target was already up.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_setDescriptor()
{
  if (_bodyLen < 247)
  {
    _replyStatus(kRspFailed);
    return;
  }
  const uint16_t flash = static_cast<uint16_t>(
    _body()[244] + (static_cast<uint16_t>(_body()[245]) << 8));
  const uint16_t eeprom = _body()[246];
  _flashPage = flash == 0 ? 64 : flash;
  _eepromPage = eeprom == 0 ? 32 : eeprom;
  _nvmVersion = _flashPage >= 256 ? 2 : 1;
  _startBreak(Pending::Descriptor);
}

/**
 * Stores Control A as 0x00 after the break, then replies OK.
 * 0x00 is the reset value, so the guard stays 128 bit times, about
 * 1.1 ms at 115200. The pull-up stays on until the reply read starts,
 * and a 2-bit guard lets the target pulse before that. This store is
 * the SYNCH that keeps a fuse-enabled pin from disabling itself.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_finishDescriptor()
{
  if (!_stcs(kRegControlA, 0x00))
  {
    _replyNoPower();
    return;
  }
  _connected = true;
  _replyOk();
}

/**
 * Enters programming mode. A lone enter still breaks when the
 * descriptor has not connected yet.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_beginEnter()
{
  if (!_connected)
  {
    _startBreak(Pending::Enter);
    return;
  }
  _linkOk = _uartAttached;
  _enterProgmode();
}

/**
 * Follows the jtagice enter sequence for NVM version 1. Status 0x82
 * applies the NVM key. Status 0x08 only clears the page buffer.
 * The body then stores the short guard.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_enterProgmode()
{
  if (!_enterTarget())
  {
    return;
  }
  _replyOk();
}

/**
 * Enter body. Status 0x82 applies the NVM key. Status 0x08 only
 * clears the page buffer. A successful enter stores Control A 0x03.
 * Chip erase does not call this. On failure the USB error reply
 * is already queued.
 *
 * @return True when the part accepted programming mode.
 */
bool UpdiProgrammer::_enterTarget()
{
  const int raw = _ldcs(kRegSysStatus);
  if (raw < 0)
  {
    _replyNoPower();
    return false;
  }
  const uint8_t status = static_cast<uint8_t>(raw & 0xEF);
  if (status == 0x21 || status == 0xA2 || status == 0x82)
  {
    if (!_cpuReset())
    {
      _replyNoPower();
      return false;
    }
    const int lock = _ldcs(kRegSysStatus);
    if (lock < 0)
    {
      _replyNoPower();
      return false;
    }
    if ((lock & 0x01) != 0)
    {
      _replyStatusParam(kRspIllegalState,
                        static_cast<uint8_t>(status | 0x01));
      return false;
    }
    if (!_writeKey(kNvmKey) || !_cpuReset())
    {
      _replyNoPower();
      return false;
    }
  }
  else if (status != 0x08)
  {
    _replyStatusParam(kRspIllegalState, status);
    return false;
  }

  if (_nvmVersion == 1)
  {
    if (!_sts(kNvmBase, kNvmPbc))
    {
      _replyNoPower();
      return false;
    }
  }
  if (!_stcs(kRegControlA, kGuardAfterEnter))
  {
    _replyNoPower();
    return false;
  }
  _inProg = true;
  return true;
}

/**
 * Leaves programming mode. Status 0x08 resets the part and replies OK.
 * Status 0x82 replies no-target-power. The UART stays attached and
 * the line stays released.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_leaveProgmode()
{
  const int raw = _ldcs(kRegSysStatus);
  if (raw < 0)
  {
    _replyNoPower();
    return;
  }
  const uint8_t status = static_cast<uint8_t>(raw & 0xEF);
  if (status == 0x08)
  {
    if (_leaveTarget())
    {
      _replyOk();
    }
    else
    {
      _replyNoPower();
    }
    return;
  }
  if (status == 0x82)
  {
    _replyNoPower();
    return;
  }
  _replyStatusParam(kRspIllegalState, 0x01);
}

/**
 * NVM wait without preserving the pointer, then a CPU reset.
 *
 * @return True when the reset poll succeeded.
 */
bool UpdiProgrammer::_leaveTarget()
{
  if (_nvmVersion == 1 && !_nvmWait(false))
  {
    return false;
  }
  const bool resetOk = _cpuReset();
  _inProg = false;
  return resetOk;
}

/**
 * Tells the part to drop the UPDI peripheral.
 *
 * @return True when the store completed.
 */
bool UpdiProgrammer::_disableUpdi()
{
  const bool stored = _stcs(kRegControlB, 0x04);
  if (stored)
  {
    _connected = false;
    _inProg = false;
  }
  return stored;
}

/**
 * Erases. Chip erase of an unconnected part breaks first.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_beginErase()
{
  if (!_connected)
  {
    _startBreak(Pending::Erase);
    return;
  }
  _linkOk = _uartAttached;
  _erase();
}

/**
 * Chip erase writes the erase key, checks ASI_KEY_STATUS bit 3,
 * pulses reset, and waits until LOCKSTATUS clears. A flash page
 * erase stores 0xFF and commits ER. EEPROM page erase is a no-op
 * because the write erases.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_erase()
{
  if (_bodyLen < 2)
  {
    _replyStatus(kRspFailed);
    return;
  }
  const uint8_t kind = _body()[1];
  if (kind == kEraseChip)
  {
    if (!_writeKey(kEraseKey))
    {
      _replyNoPower();
      return;
    }
    const int keyStatus = _ldcs(kRegKeyStatus);
    if (keyStatus < 0 || (static_cast<uint8_t>(keyStatus) & 0x08) == 0)
    {
      _replyNoPower();
      return;
    }
    const LockWait waited = _waitLockClear();
    if (waited == LockWait::PulseNotSent)
    {
      _replyNoPower();
      return;
    }
    _inProg = false;
    if (waited != LockWait::Unlocked)
    {
      _replyNoPower();
      return;
    }
    _replyOk();
    return;
  }
  if (kind == kEraseEepromPage || kind == kEraseUsersig)
  {
    _replyOk();
    return;
  }
  if (kind == kEraseAppPage || kind == kEraseBootPage)
  {
    if (_nvmVersion != 1 || _bodyLen < 4)
    {
      _replyStatus(kRspFailed);
      return;
    }
    const uint16_t address = static_cast<uint16_t>(
      _body()[2] | (_body()[3] << 8));
    if (!_nvmWait(false) || !_sts(address, 0xFF) || !_nvmCommand(false, kNvmEr))
    {
      _replyNoPower();
      return;
    }
    _replyOk();
    return;
  }
  _replyStatus(kRspFailed);
}

/**
 * Reads count bytes at the packet address. The address is not adjusted.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_readMemory()
{
  const int mode = _ldcs(kRegSysStatus);
  if (mode < 0)
  {
    _replyNoPower();
    return;
  }
  if (mode != 0x08)
  {
    _replyStatusParam(kRspIllegalState, 0x01);
    return;
  }
  if (_bodyLen < 8 || _nvmVersion != 1)
  {
    _replyStatus(kRspFailed);
    return;
  }
  const uint16_t count = static_cast<uint16_t>(_body()[2] | (_body()[3] << 8));
  const uint16_t address = static_cast<uint16_t>(_body()[6] | (_body()[7] << 8));
  if (count == 0 || count > kMaxBody - 1)
  {
    _replyStatus(kRspFailed);
    return;
  }
  if (!_stptr(address) || !_rep(static_cast<uint8_t>(count - 1)))
  {
    _replyNoPower();
    return;
  }
  uint8_t body[1 + kMaxBody];
  body[0] = kRspMemory;
  const int first = _ldinc();
  if (first < 0)
  {
    _replyNoPower();
    return;
  }
  body[1] = static_cast<uint8_t>(first);
  for (uint16_t index = 1; index < count; ++index)
  {
    const int value = _get();
    if (value < 0)
    {
      _replyNoPower();
      return;
    }
    body[1 + index] = static_cast<uint8_t>(value);
  }
  _reply(body, static_cast<uint16_t>(count + 1));
}

/**
 * Writes the packet payload. Flash uses WP. EEPROM and the user row
 * use ERWP. The address is passed through unchanged.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_writeMemory()
{
  const int mode = _ldcs(kRegSysStatus);
  if (mode < 0)
  {
    _replyNoPower();
    return;
  }
  if (mode != 0x08)
  {
    _replyStatusParam(kRspIllegalState, 0x01);
    return;
  }
  if (_bodyLen < 8 || _nvmVersion != 1)
  {
    _replyStatus(kRspFailed);
    return;
  }
  const uint8_t memType = _body()[1];
  const uint16_t length = static_cast<uint16_t>(_body()[2] | (_body()[3] << 8));
  const uint16_t address = static_cast<uint16_t>(_body()[6] | (_body()[7] << 8));
  if (static_cast<uint32_t>(_bodyLen) < 10u + length)
  {
    _replyStatus(kRspFailed);
    return;
  }
  bool wrote = false;
  if (memType == kMemFuse || memType == kMemLock)
  {
    wrote = length >= 1 && _writeFuse(address, _body()[10]);
  }
  else if (memType == kMemFlash || memType == kMemBoot)
  {
    wrote = _bufferedWrite(address, _body() + 10, length, _flashPage, kNvmWp);
  }
  else if (memType == kMemEeprom || memType == kMemEepromXmega ||
           memType == kMemUsersig)
  {
    wrote = _bufferedWrite(address, _body() + 10, length, _eepromPage, kNvmErwp);
  }
  else
  {
    _replyStatus(kRspIllegalMemory);
    return;
  }
  if (!wrote)
  {
    _replyNoPower();
    return;
  }
  _replyOk();
}

/**
 * Writes one fuse byte through the NVM data and address registers.
 *
 * @param address Fuse address.
 * @param data Byte to store.
 * @return True when the command completed.
 */
bool UpdiProgrammer::_writeFuse(uint16_t address, uint8_t data)
{
  if (!_stptr(kNvmData))
  {
    return false;
  }
  if (!_stinc(data) || !_stinc(0x00))
  {
    return false;
  }
  if (!_stinc(static_cast<uint8_t>(address & 0xFF)) ||
      !_stinc(static_cast<uint8_t>(address >> 8)))
  {
    return false;
  }
  return _nvmCommand(false, kNvmWfu);
}

/**
 * Sends an unaligned head, then full pages. Each piece is committed
 * with the caller's NVM command while the pointer is preserved.
 *
 * @param address First address.
 * @param data Payload.
 * @param length Payload length.
 * @param page Page size in bytes.
 * @param command NVM commit command.
 * @return True when the link accepted every byte.
 */
bool UpdiProgrammer::_bufferedWrite(uint16_t address, const uint8_t* data,
                                    uint16_t length, uint16_t page,
                                    uint8_t command)
{
  if (page == 0)
  {
    page = 64;
  }
  if (!_stptr(address))
  {
    return false;
  }
  uint16_t offset = 0;
  uint16_t unaligned = static_cast<uint16_t>((0u - address) & (page - 1u));
  if (unaligned > length)
  {
    unaligned = length;
  }
  if (unaligned > 255)
  {
    unaligned = 255;
  }
  if (unaligned != 0)
  {
    if (!_sendBlock(data, static_cast<uint8_t>(unaligned)) ||
        !_nvmCommand(true, command))
    {
      return false;
    }
    offset = unaligned;
  }
  while (offset < length)
  {
    uint16_t chunk = static_cast<uint16_t>(length - offset);
    if (chunk > page)
    {
      chunk = page;
    }
    if (chunk > 255)
    {
      chunk = 255;
    }
    if (!_sendBlock(data + offset, static_cast<uint8_t>(chunk)) ||
        !_nvmCommand(true, command))
    {
      return false;
    }
    offset = static_cast<uint16_t>(offset + chunk);
  }
  return true;
}

/**
 * Waits for NVM, repeats count-1, stores the first byte with STINC,
 * then stores the rest as raw bytes that each return an ack.
 *
 * @param data Bytes to store.
 * @param count Number of bytes.
 * @return True when every ack matched.
 */
bool UpdiProgrammer::_sendBlock(const uint8_t* data, uint8_t count)
{
  if (count == 0)
  {
    return true;
  }
  const uint8_t extra = static_cast<uint8_t>(count - 1);
  if (!_nvmWait(true) || !_rep(extra) || !_stinc(data[0]))
  {
    return false;
  }
  for (uint8_t index = 1; index < count; ++index)
  {
    if (!_put(data[index]) || !_ack())
    {
      return false;
    }
  }
  return true;
}


// ========== NVM ==========

/**
 * Polls status at 0x1002 until bit 0 and bit 1 are clear or
 * kNvmWaitMs elapses. An idle first status returns immediately.
 *
 * @param preserve True to save and restore the pointer.
 * @return False on a link failure or a stuck busy bit.
 */
bool UpdiProgrammer::_nvmWait(bool preserve)
{
  uint16_t pointer = 0;
  if (preserve && !_ldptr(pointer))
  {
    return false;
  }
  const uint32_t start = _clock.millis();
  while (static_cast<uint32_t>(_clock.millis() - start) < kNvmWaitMs)
  {
    const int status = _lds(kNvmStatus);
    if (status < 0)
    {
      return false;
    }
    if ((status & 0x03) == 0)
    {
      if (preserve)
      {
        return _stptr(pointer);
      }
      return true;
    }
  }
  return false;
}

/**
 * Stores a command at the NVM control register.
 *
 * @param preserve True to save and restore the pointer.
 * @param command Command byte.
 * @return False when the link fails.
 */
bool UpdiProgrammer::_nvmCommand(bool preserve, uint8_t command)
{
  uint16_t pointer = 0;
  if (preserve && !_ldptr(pointer))
  {
    return false;
  }
  if (!_sts(kNvmBase, command))
  {
    return false;
  }
  if (preserve)
  {
    return _stptr(pointer);
  }
  return true;
}

/**
 * Pulses reset and polls system status until LOCKSTATUS (bit 0)
 * clears. A missing byte is tried again until kResetWaitMs elapses.
 * An echo mismatch on a reset store returns before the poll. An
 * echo mismatch on a status load ends the poll. A late byte already
 * buffered is dropped before the next load. The progmode flag is
 * left alone.
 *
 * @return Unlocked when bit 0 was clear. NotUnlocked when both
 *         stores were sent and bit 0 stayed set. PulseNotSent when
 *         a reset store failed.
 */
UpdiProgrammer::LockWait UpdiProgrammer::_waitLockClear()
{
  if (!_stcs(kRegReset, kResetOn) || !_stcs(kRegReset, 0x00))
  {
    return LockWait::PulseNotSent;
  }
  const uint32_t start = _clock.millis();
  while (static_cast<uint32_t>(_clock.millis() - start) < kResetWaitMs)
  {
    const uint8_t frame[] = {
      kSynch, static_cast<uint8_t>(0x80 + kRegSysStatus)};
    if (!_send(frame, sizeof(frame)))
    {
      return LockWait::NotUnlocked;
    }
    const int status = _get();
    if (status >= 0 && (status & 0x01) == 0)
    {
      return LockWait::Unlocked;
    }
    if (status < 0)
    {
      if (!_uartAttached)
      {
        return LockWait::NotUnlocked;
      }
      _linkOk = true;
      while (_uart.available() > 0)
      {
        _uart.read();
      }
    }
  }
  return LockWait::NotUnlocked;
}

/**
 * Holds reset, releases it, and polls system status until bit 1, 2,
 * or 3 is set. A clear byte or a missing byte is tried again until
 * kResetWaitMs elapses. An echo mismatch fails immediately. A late
 * byte already buffered is dropped before the next load.
 *
 * @return False when the window ends with those bits clear, or an
 *         echo mismatches.
 */
bool UpdiProgrammer::_cpuReset()
{
  if (!_stcs(kRegReset, kResetOn) || !_stcs(kRegReset, 0x00))
  {
    return false;
  }
  const uint32_t start = _clock.millis();
  while (static_cast<uint32_t>(_clock.millis() - start) < kResetWaitMs)
  {
    const uint8_t frame[] = {
      kSynch, static_cast<uint8_t>(0x80 + kRegSysStatus)};
    if (!_send(frame, sizeof(frame)))
    {
      return false;
    }
    const int status = _get();
    if (status >= 0 && (status & 0x0E) != 0)
    {
      return true;
    }
    if (status < 0)
    {
      if (!_uartAttached)
      {
        return false;
      }
      _linkOk = true;
      while (_uart.available() > 0)
      {
        _uart.read();
      }
    }
  }
  return false;
}


// ========== UPDI link ==========

/**
 * Sends SYNCH, the key opcode, and eight key bytes as one write.
 *
 * @param key Wire-order key.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_writeKey(const uint8_t* key)
{
  uint8_t frame[10];
  frame[0] = kSynch;
  frame[1] = 0xE0;
  for (uint8_t index = 0; index < 8; ++index)
  {
    frame[2 + index] = key[index];
  }
  return _send(frame, sizeof(frame));
}

/**
 * STCS. One three-byte write, no response.
 *
 * @param reg Control register index.
 * @param data Value.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_stcs(uint8_t reg, uint8_t data)
{
  const uint8_t frame[] = {
    kSynch, static_cast<uint8_t>(0xC0 + reg), data};
  return _send(frame, sizeof(frame));
}

/**
 * LDCS. One two-byte write, then one byte back.
 *
 * @param reg Control register index.
 * @return Register value, or -1.
 */
int UpdiProgrammer::_ldcs(uint8_t reg)
{
  const uint8_t frame[] = {
    kSynch, static_cast<uint8_t>(0x80 + reg)};
  if (!_send(frame, sizeof(frame)))
  {
    return -1;
  }
  return _get();
}

/**
 * LDS of one byte at a 16-bit address.
 *
 * @param address UPDI address.
 * @return Data byte, or -1.
 */
int UpdiProgrammer::_lds(uint16_t address)
{
  const uint8_t frame[] = {
    kSynch,
    0x04,
    static_cast<uint8_t>(address & 0xFF),
    static_cast<uint8_t>(address >> 8)};
  if (!_send(frame, sizeof(frame)))
  {
    return -1;
  }
  return _get();
}

/**
 * STS of one byte. The address is one write and is acknowledged,
 * then the data byte is its own write.
 *
 * @param address UPDI address.
 * @param data Byte to store.
 * @return False when an ack is not 0x40.
 */
bool UpdiProgrammer::_sts(uint16_t address, uint8_t data)
{
  const uint8_t frame[] = {
    kSynch,
    0x44,
    static_cast<uint8_t>(address & 0xFF),
    static_cast<uint8_t>(address >> 8)};
  if (!_send(frame, sizeof(frame)) || !_ack() || !_put(data))
  {
    return false;
  }
  return _ack();
}

/**
 * Sets the 16-bit address pointer. One four-byte write, then an ack.
 *
 * @param address Pointer value.
 * @return False when the ack is missing.
 */
bool UpdiProgrammer::_stptr(uint16_t address)
{
  const uint8_t frame[] = {
    kSynch,
    0x69,
    static_cast<uint8_t>(address & 0xFF),
    static_cast<uint8_t>(address >> 8)};
  if (!_send(frame, sizeof(frame)))
  {
    return false;
  }
  return _ack();
}

/**
 * Reads the 16-bit address pointer.
 *
 * @param address Receives the pointer.
 * @return False when a byte is missing.
 */
bool UpdiProgrammer::_ldptr(uint16_t& address)
{
  const uint8_t frame[] = {kSynch, 0x29};
  if (!_send(frame, sizeof(frame)))
  {
    return false;
  }
  const int low = _get();
  const int high = _get();
  if (low < 0 || high < 0)
  {
    return false;
  }
  address = static_cast<uint16_t>(low | (high << 8));
  return true;
}

/**
 * STINC of one byte. One three-byte write, then an ack.
 *
 * @param data Byte to store.
 * @return False when the ack is missing.
 */
bool UpdiProgrammer::_stinc(uint8_t data)
{
  const uint8_t frame[] = {kSynch, 0x64, data};
  if (!_send(frame, sizeof(frame)))
  {
    return false;
  }
  return _ack();
}

/**
 * LDINC of one byte.
 *
 * @return Data byte, or -1.
 */
int UpdiProgrammer::_ldinc()
{
  const uint8_t frame[] = {kSynch, 0x24};
  if (!_send(frame, sizeof(frame)))
  {
    return -1;
  }
  return _get();
}

/**
 * REP. The next instruction runs repeats extra times.
 *
 * @param repeats Extra count, already capped at 255 by the type.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_rep(uint8_t repeats)
{
  const uint8_t frame[] = {kSynch, 0xA0, repeats};
  return _send(frame, sizeof(frame));
}

/**
 * Writes one instruction and requires each local echo to match.
 *
 * @param data Instruction bytes, including SYNCH.
 * @param length Number of bytes.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_send(const uint8_t* data, size_t length)
{
  if (!_linkOk || data == nullptr || length == 0)
  {
    return false;
  }
  _uart.write(data, length);
  for (size_t index = 0; index < length; ++index)
  {
    const int echo = _uart.read();
    if (echo != static_cast<int>(data[index]))
    {
      _linkOk = false;
      return false;
    }
  }
  return true;
}

/**
 * Writes one byte and requires the same byte back as the local echo.
 *
 * @param byte Byte to send.
 * @return False when the echo mismatches.
 */
bool UpdiProgrammer::_put(uint8_t byte)
{
  return _send(&byte, 1);
}

/**
 * Reads one byte from the target, after any echo has been consumed.
 *
 * @return Byte value, or -1.
 */
int UpdiProgrammer::_get()
{
  if (!_linkOk)
  {
    return -1;
  }
  const int value = _uart.read();
  if (value < 0)
  {
    _linkOk = false;
    return -1;
  }
  return value;
}

/**
 * Requires the next target byte to be the UPDI ack.
 *
 * @return False when it is missing or not 0x40.
 */
bool UpdiProgrammer::_ack()
{
  return _get() == kAck;
}

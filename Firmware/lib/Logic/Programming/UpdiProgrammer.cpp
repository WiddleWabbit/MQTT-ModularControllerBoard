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

const uint8_t kRegControlA = 2;
const uint8_t kRegControlB = 3;
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
 * polynomial 0xA001. The check value of "123456789" is 0x4B37.
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
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
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
 * Stores the UPDI pin and leaves it idle. The UART stays detached
 * until a connect break finishes.
 *
 * @param line Slot CS pin.
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
 * starts or the port is empty.
 *
 * @return Nothing.
 */
void UpdiProgrammer::update()
{
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
 * Detaches the UART and returns the line to an input with pull-up.
 *
 * @return Nothing.
 */
void UpdiProgrammer::shutdown()
{
  if (_uartAttached)
  {
    _uart.detach();
    _uartAttached = false;
  }
  if (_line != nullptr)
  {
    _line->setMode(PinMode::DigitalInputPullup);
  }
  _breakPhase = BreakPhase::Idle;
  _pending = Pending::None;
  _connected = false;
  _inProg = false;
  _linkOk = false;
  _rx = Rx::Start;
  _crcCount = 0;
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
 * Checks CRC-16/ARC over the start marker through the body. A mismatch
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
        _leaveTarget();
        _disableUpdi();
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
      if (_connected)
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
 * Drops the UART and holds the line low. One later update() moves
 * each of the three timed phases.
 *
 * @param pending Command to run when the line is released.
 * @return Nothing.
 */
void UpdiProgrammer::_startBreak(Pending pending)
{
  _pending = pending;
  _uart.detach();
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
 * Moves 25 ms low, 1 ms high, then 25 ms low. The completing call
 * releases the line and attaches the UART at 115200.
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
      _line->write(true);
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
    if (_line != nullptr)
    {
      _line->write(false);
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
  _uart.attach(_gpio, kBaud);
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
 * Enables UPDI by storing 0x06 in Control A, then replies OK.
 *
 * @return Nothing.
 */
void UpdiProgrammer::_finishDescriptor()
{
  if (!_stcs(kRegControlA, 0x06))
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
 * Shared enter body used by enter-progmode and by chip erase. On
 * failure it has already queued the USB error reply.
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
 * Chip erase applies the erase key, resets, and re-enters. A flash
 * page erase stores 0xFF and commits ER. EEPROM page erase is a no-op
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
    if (!_writeKey(kEraseKey) || !_cpuReset())
    {
      _replyNoPower();
      return;
    }
    if (_enterTarget())
    {
      _replyOk();
    }
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
 * Polls status at 0x1002 at most eight times. Bit 0 and bit 1 clear
 * means the controller is idle.
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
  bool idle = false;
  for (uint8_t attempt = 0; attempt < 8; ++attempt)
  {
    const int status = _lds(kNvmStatus);
    if (status < 0)
    {
      return false;
    }
    if ((status & 0x03) == 0)
    {
      idle = true;
      break;
    }
  }
  if (!idle)
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
 * Holds reset, releases it, and polls system status at most four times
 * until bits 1..3 are not all clear.
 *
 * @return False when the part never answers.
 */
bool UpdiProgrammer::_cpuReset()
{
  if (!_stcs(kRegReset, kResetOn) || !_stcs(kRegReset, 0x00))
  {
    return false;
  }
  for (uint8_t attempt = 0; attempt < 4; ++attempt)
  {
    const int status = _ldcs(kRegSysStatus);
    if (status < 0)
    {
      return false;
    }
    if ((status & 0x0E) != 0)
    {
      return true;
    }
  }
  return false;
}


// ========== UPDI link ==========

/**
 * Sends SYNCH, the key opcode, and eight key bytes.
 *
 * @param key Wire-order key.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_writeKey(const uint8_t* key)
{
  if (!_put(kSynch) || !_put(0xE0))
  {
    return false;
  }
  for (uint8_t index = 0; index < 8; ++index)
  {
    if (!_put(key[index]))
    {
      return false;
    }
  }
  return true;
}

/**
 * STCS. Three bytes, no response.
 *
 * @param reg Control register index.
 * @param data Value.
 * @return False when an echo mismatches.
 */
bool UpdiProgrammer::_stcs(uint8_t reg, uint8_t data)
{
  return _put(kSynch) &&
         _put(static_cast<uint8_t>(0xC0 + reg)) &&
         _put(data);
}

/**
 * LDCS. Two bytes out, one byte back.
 *
 * @param reg Control register index.
 * @return Register value, or -1.
 */
int UpdiProgrammer::_ldcs(uint8_t reg)
{
  if (!_put(kSynch) || !_put(static_cast<uint8_t>(0x80 + reg)))
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
  if (!_put(kSynch) || !_put(0x04) ||
      !_put(static_cast<uint8_t>(address & 0xFF)) ||
      !_put(static_cast<uint8_t>(address >> 8)))
  {
    return -1;
  }
  return _get();
}

/**
 * STS of one byte. The address is acknowledged, then the data.
 *
 * @param address UPDI address.
 * @param data Byte to store.
 * @return False when an ack is not 0x40.
 */
bool UpdiProgrammer::_sts(uint16_t address, uint8_t data)
{
  if (!_put(kSynch) || !_put(0x44) ||
      !_put(static_cast<uint8_t>(address & 0xFF)) ||
      !_put(static_cast<uint8_t>(address >> 8)) ||
      !_ack() || !_put(data))
  {
    return false;
  }
  return _ack();
}

/**
 * Sets the 16-bit address pointer.
 *
 * @param address Pointer value.
 * @return False when the ack is missing.
 */
bool UpdiProgrammer::_stptr(uint16_t address)
{
  if (!_put(kSynch) || !_put(0x69) ||
      !_put(static_cast<uint8_t>(address & 0xFF)) ||
      !_put(static_cast<uint8_t>(address >> 8)))
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
  if (!_put(kSynch) || !_put(0x29))
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
 * STINC of one byte.
 *
 * @param data Byte to store.
 * @return False when the ack is missing.
 */
bool UpdiProgrammer::_stinc(uint8_t data)
{
  if (!_put(kSynch) || !_put(0x64) || !_put(data))
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
  if (!_put(kSynch) || !_put(0x24))
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
  return _put(kSynch) && _put(0xA0) && _put(repeats);
}

/**
 * Writes one byte and requires the same byte back as the local echo.
 *
 * @param byte Byte to send.
 * @return False when the echo mismatches.
 */
bool UpdiProgrammer::_put(uint8_t byte)
{
  if (!_linkOk)
  {
    return false;
  }
  _uart.write(&byte, 1);
  const int echo = _uart.read();
  if (echo != static_cast<int>(byte))
  {
    _linkOk = false;
    return false;
  }
  return true;
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

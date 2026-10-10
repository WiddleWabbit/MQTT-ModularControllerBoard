#pragma once

#include <cstddef>
#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IDigitalPin.h"
#include "IHalfDuplexUart.h"
#include "IProgrammer.h"

/**
 * jtag2updi programmer for a tinyAVR 1-series part. The host link is
 * the USB byte stream. The target link is one open-drain GPIO at
 * 115200 8E2. NVM controller version 1 (flash page under 256 bytes)
 * is the path this speaks. A larger page is recorded and then refused
 * for memory commands.
 */
class UpdiProgrammer : public IProgrammer
{
public:
  static const uint32_t kBaud = 115200;
  static const uint32_t kBreakLowMs = 25;
  static const uint32_t kBreakHighMs = 1;
  static const size_t kMaxBody = 512;

  /**
   * Creates a programmer. The line is bound later by start().
   *
   * @param port Binary USB byte stream.
   * @param uart Single-wire UART for the UPDI pin.
   * @param clock Monotonic clock for the double break.
   */
  UpdiProgrammer(IBytePort& port, IHalfDuplexUart& uart, IClock& clock);

  /**
   * Stores the UPDI pin and leaves it as an input with pull-up.
   * Does not attach the UART.
   *
   * @param line Slot CS pin used as the UPDI wire.
   * @param gpio GPIO number passed to attach().
   * @return Nothing.
   */
  void start(IDigitalPin& line, uint8_t gpio) override;

  /**
   * Advances a double break by one phase, then consumes host bytes.
   * Host bytes wait while a break is in progress.
   *
   * @return Nothing.
   */
  void update() override;

  /**
   * Detaches the UART and returns the line to an input with pull-up.
   *
   * @return Nothing.
   */
  void shutdown() override;

  /**
   * Counts bytes accepted from the host port.
   *
   * @return Monotonic count of accepted bytes.
   */
  uint32_t activityCount() const override;

private:
  enum class Rx : uint8_t
  {
    Start,
    Seq,
    Size,
    Token,
    Body,
    Crc
  };

  enum class BreakPhase : uint8_t
  {
    Idle,
    Low1,
    High,
    Low2
  };

  enum class Pending : uint8_t
  {
    None,
    Descriptor,
    Enter,
    Erase
  };

  IBytePort& _port;
  IHalfDuplexUart& _uart;
  IClock& _clock;
  IDigitalPin* _line = nullptr;
  uint8_t _gpio = 0;
  Rx _rx = Rx::Start;
  BreakPhase _breakPhase = BreakPhase::Idle;
  Pending _pending = Pending::None;
  bool _connected = false;
  bool _inProg = false;
  bool _uartAttached = false;
  bool _linkOk = false;
  uint8_t _nvmVersion = 1;
  uint8_t _emuMode = 0;
  uint8_t _baudCode = 4;
  uint16_t _flashPage = 64;
  uint16_t _eepromPage = 32;
  uint16_t _sequence = 0;
  uint32_t _bodyLen = 0;
  uint32_t _rawCount = 0;
  uint32_t _breakMark = 0;
  uint32_t _activity = 0;
  uint8_t _crcBytes[2] = {};
  uint8_t _crcCount = 0;
  uint8_t _raw[8 + kMaxBody] = {};

  /**
   * Accepts one host byte into the jtagice frame parser.
   *
   * @param byte Next protocol byte.
   * @return Nothing.
   */
  void _acceptByte(uint8_t byte);

  /**
   * Checks the frame CRC and runs the command.
   *
   * @return Nothing.
   */
  void _onFrame();

  /**
   * Runs one jtagice command from the current body.
   *
   * @return Nothing.
   */
  void _dispatch();

  /**
   * Starts the double break and remembers which command follows it.
   *
   * @param pending Command to run when the break finishes.
   * @return Nothing.
   */
  void _startBreak(Pending pending);

  /**
   * Advances the double break by one phase.
   *
   * @return True on the update that releases the line and attaches.
   */
  bool _advanceBreak();

  /**
   * Runs the command that was waiting on the break.
   *
   * @return Nothing.
   */
  void _runPending();

  /**
   * Applies flash and EEPROM page sizes, then breaks and enables UPDI.
   *
   * @return Nothing.
   */
  void _setDescriptor();

  /**
   * Stores Control A after the break and replies OK.
   *
   * @return Nothing.
   */
  void _finishDescriptor();

  /**
   * Enters programming mode. Breaks first when the target is not up.
   *
   * @return Nothing.
   */
  void _beginEnter();

  /**
   * Speaks the enter-progmode sequence on an attached link.
   *
   * @return Nothing.
   */
  void _enterProgmode();

  /**
   * Leaves programming mode. The UART stays attached.
   *
   * @return Nothing.
   */
  void _leaveProgmode();

  /**
   * Reset, NVM key, and page-buffer clear. No USB reply.
   *
   * @return True when the part is in programming mode.
   */
  bool _enterTarget();

  /**
   * Waits for NVM, resets the part, and clears the progmode flag.
   *
   * @return True when the reset poll saw the part come back.
   */
  bool _leaveTarget();

  /**
   * Disables the UPDI peripheral and clears the connected flag.
   *
   * @return True when the store completed.
   */
  bool _disableUpdi();

  /**
   * Erases. Breaks first when the target is not up.
   *
   * @return Nothing.
   */
  void _beginErase();

  /**
   * Runs chip or page erase on an attached link.
   *
   * @return Nothing.
   */
  void _erase();

  /**
   * Reads memory at the absolute UPDI address in the packet.
   *
   * @return Nothing.
   */
  void _readMemory();

  /**
   * Writes flash, EEPROM, user row, fuse, or lock bytes.
   *
   * @return Nothing.
   */
  void _writeMemory();

  /**
   * Writes one fuse or lock byte through the NVM data registers.
   *
   * @param address Fuse address from the packet.
   * @param data Byte to store.
   * @return True when every acknowledge matched.
   */
  bool _writeFuse(uint16_t address, uint8_t data);

  /**
   * Writes a page-buffered block, committing each full or partial page.
   *
   * @param address First UPDI address.
   * @param data Bytes from the packet.
   * @param length Number of bytes.
   * @param page Page size. Zero is treated as 64.
   * @param command NVM command used to commit. WP or ERWP.
   * @return True when every acknowledge matched.
   */
  bool _bufferedWrite(uint16_t address, const uint8_t* data, uint16_t length,
                      uint16_t page, uint8_t command);

  /**
   * Sends count data bytes with one REP, then commits nothing.
   *
   * @param data Bytes to store at the current pointer.
   * @param count Number of bytes, from 1 to 255.
   * @return True when every acknowledge matched.
   */
  bool _sendBlock(const uint8_t* data, uint8_t count);

  /**
   * Polls NVM status. Optionally saves and restores the pointer.
   *
   * @param preserve True to restore the pointer around the poll.
   * @return False when the link fails or the controller stays busy.
   */
  bool _nvmWait(bool preserve);

  /**
   * Stores an NVM command at 0x1000. Optionally saves the pointer.
   *
   * @param preserve True to restore the pointer around the store.
   * @param command NVM command byte.
   * @return False when the link fails.
   */
  bool _nvmCommand(bool preserve, uint8_t command);

  /**
   * Asserts and releases reset, then polls until the part responds.
   *
   * @return False when the poll budget runs out or the link fails.
   */
  bool _cpuReset();

  /**
   * Writes an 8-byte UPDI key. No response byte is expected.
   *
   * @param key Eight key bytes, already in wire order.
   * @return False when the echo check fails.
   */
  bool _writeKey(const uint8_t* key);

  /**
   * Stores a control register. No response byte is expected.
   *
   * @param reg Register index.
   * @param data Value to store.
   * @return False when the echo check fails.
   */
  bool _stcs(uint8_t reg, uint8_t data);

  /**
   * Loads a control register.
   *
   * @param reg Register index.
   * @return Register value, or -1 when the link fails.
   */
  int _ldcs(uint8_t reg);

  /**
   * Loads one data byte by 16-bit address.
   *
   * @param address UPDI address.
   * @return Data byte, or -1 when the link fails.
   */
  int _lds(uint16_t address);

  /**
   * Stores one data byte by 16-bit address. Requires two 0x40 acks.
   *
   * @param address UPDI address.
   * @param data Byte to store.
   * @return False when an acknowledge is missing.
   */
  bool _sts(uint16_t address, uint8_t data);

  /**
   * Sets the 16-bit pointer. Requires one 0x40 ack.
   *
   * @param address Pointer value.
   * @return False when the acknowledge is missing.
   */
  bool _stptr(uint16_t address);

  /**
   * Reads the 16-bit pointer.
   *
   * @param address Receives the pointer.
   * @return False when a byte is missing.
   */
  bool _ldptr(uint16_t& address);

  /**
   * Stores one byte and increments the pointer. Requires one ack.
   *
   * @param data Byte to store.
   * @return False when the acknowledge is missing.
   */
  bool _stinc(uint8_t data);

  /**
   * Loads one byte and increments the pointer.
   *
   * @return Data byte, or -1 when the link fails.
   */
  int _ldinc();

  /**
   * Arms a repeat of the next instruction.
   *
   * @param repeats Extra executions after the first. Capped by the type.
   * @return False when the echo check fails.
   */
  bool _rep(uint8_t repeats);

  /**
   * Writes one byte and consumes its echo.
   *
   * @param byte Byte to send.
   * @return False when the echo is missing or different.
   */
  bool _put(uint8_t byte);

  /**
   * Reads one target byte.
   *
   * @return Byte value, or -1 when none is available.
   */
  int _get();

  /**
   * Reads one byte and requires the UPDI ack value 0x40.
   *
   * @return False when the byte is missing or not 0x40.
   */
  bool _ack();

  /**
   * Points at the current frame body.
   *
   * @return Body bytes. Valid until the next host byte is parsed.
   */
  const uint8_t* _body() const;

  /**
   * Replies with a status byte and the frame's sequence number.
   *
   * @param status jtagice response code.
   * @return Nothing.
   */
  void _replyStatus(uint8_t status);

  /**
   * Replies with a status byte and one parameter.
   *
   * @param status jtagice response code.
   * @param param Extra status byte.
   * @return Nothing.
   */
  void _replyStatusParam(uint8_t status, uint8_t param);

  /**
   * Replies OK.
   *
   * @return Nothing.
   */
  void _replyOk();

  /**
   * Replies that the target link failed.
   *
   * @return Nothing.
   */
  void _replyNoPower();

  /**
   * Writes one jtagice answer frame.
   *
   * @param body Answer body.
   * @param length Body length.
   * @return Nothing.
   */
  void _reply(const uint8_t* body, uint16_t length);
};

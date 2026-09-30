#pragma once

#include <cstddef>
#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IDigitalPin.h"
#include "ISpiMaster.h"

/**
 * STK500v1 programmer for an AVR held in reset on one GPIO.
 * Speaks the Arduino-as-ISP subset avrdude uses: sync, parameters,
 * device setup, programming enable, universal, paged read/write,
 * signature, and chip erase. SPI stays at kSpiClockHz.
 */
class IspProgrammer
{
public:
  static const uint32_t kSpiClockHz = 125000;
  static const uint32_t kResetSettleMs = 20;
  static const uint32_t kFlashPageWriteMs = 5;
  static const uint32_t kEepromWriteMs = 10;
  static const uint32_t kChipEraseMs = 10;
  static const size_t kMaxPageBytes = 256;

  /**
   * Creates a programmer. The reset pin is slot 1 CS on the real board.
   *
   * @param port Binary USB byte stream.
   * @param spi SPI master. SCK must idle low after begin().
   * @param reset Target reset. Driven low only while programming.
   * @param clock Monotonic clock for reset and write waits.
   */
  IspProgrammer(IBytePort& port, ISpiMaster& spi, IDigitalPin& reset,
                IClock& clock);

  /**
   * Consumes queued STK500 bytes and continues any timed SPI step.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Releases SPI and returns reset to an input with pull-up.
   *
   * @return Nothing.
   */
  void shutdown();

  /**
   * Counts bytes accepted from the port. Used to detect idle time.
   *
   * @return Monotonic count of accepted bytes.
   */
  uint32_t activityCount() const;

private:
  enum class Phase : uint8_t
  {
    Command,
    Fixed,
    UnknownEop,
    PageHeader,
    PageBody,
    Discard,
    Eop
  };

  enum class AfterWait : uint8_t
  {
    None,
    ProgrammingEnable,
    ReplyOk,
    ReplyEraseByte,
    EepromNext
  };

  IBytePort& _port;
  ISpiMaster& _spi;
  IDigitalPin& _reset;
  IClock& _clock;
  Phase _phase = Phase::Command;
  AfterWait _afterWait = AfterWait::None;
  bool _waiting = false;
  bool _spiActive = false;
  bool _programming = false;
  bool _oversize = false;
  bool _pageIsRead = false;
  uint8_t _cmd = 0;
  uint8_t _fixed[21] = {};
  uint8_t _fixedCount = 0;
  uint8_t _fixedNeed = 0;
  uint8_t _header[3] = {};
  uint8_t _headerCount = 0;
  uint8_t _page[kMaxPageBytes] = {};
  uint16_t _pageLen = 0;
  uint16_t _pageCount = 0;
  uint16_t _discardLeft = 0;
  uint8_t _memType = 0;
  uint16_t _address = 0;
  uint16_t _pageSizeBytes = 0;
  uint16_t _eepromAddress = 0;
  uint16_t _eepromIndex = 0;
  uint8_t _enableAttempts = 0;
  uint8_t _deferredByte = 0;
  uint8_t _parameters[256] = {};
  uint32_t _activity = 0;
  uint32_t _waitStartedAt = 0;
  uint32_t _waitMs = 0;

  /**
   * Accepts one STK500 byte.
   *
   * @param byte Next protocol byte.
   * @return Nothing.
   */
  void _accept(uint8_t byte);

  /**
   * Payload bytes before the end-of-packet marker, or -1 when unknown.
   *
   * @param cmd Command byte.
   * @return Payload length, or -1.
   */
  int _payloadLen(uint8_t cmd) const;

  /**
   * Runs a command whose payload is already buffered.
   *
   * @return Nothing.
   */
  void _dispatchFixed();

  /**
   * Runs a paged read or write after its payload and end marker.
   *
   * @return Nothing.
   */
  void _dispatchPage();

  /**
   * Starts programming mode: SPI first, then reset low.
   *
   * @return Nothing.
   */
  void _beginProgramming();

  /**
   * Sends programming enable, or pulses reset and waits to retry.
   *
   * @return Nothing.
   */
  void _tryProgrammingEnable();

  /**
   * Loads a flash page and arms the page-write wait.
   *
   * @return Nothing.
   */
  void _programFlash();

  /**
   * Starts a byte-by-byte EEPROM write.
   *
   * @return Nothing.
   */
  void _programEeprom();

  /**
   * Writes the next EEPROM byte, or replies when the buffer is done.
   *
   * @return Nothing.
   */
  void _programNextEepromByte();

  /**
   * Reads one flash or EEPROM page back to the port.
   *
   * @return Nothing.
   */
  void _readPage();

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
  void _transfer4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t* rx);

  /**
   * Arms a wait and stops reading until it elapses.
   *
   * @param next Step to run when the wait elapses.
   * @param milliseconds Wait length.
   * @return Nothing.
   */
  void _defer(AfterWait next, uint32_t milliseconds);

  /**
   * Runs the step armed by _defer.
   *
   * @param next Step to run.
   * @return Nothing.
   */
  void _continue(AfterWait next);

  /**
   * Reports whether the armed wait has elapsed.
   *
   * @return True when the wait is over.
   */
  bool _waitElapsed() const;

  /**
   * Writes one response byte.
   *
   * @param byte Response byte.
   * @return Nothing.
   */
  void _write(uint8_t byte);

  /**
   * Writes in-sync and OK.
   *
   * @return Nothing.
   */
  void _replyOk();

  /**
   * Writes in-sync and failed.
   *
   * @return Nothing.
   */
  void _replyFailed();

  /**
   * Writes not-in-sync.
   *
   * @return Nothing.
   */
  void _replyNosync();
};

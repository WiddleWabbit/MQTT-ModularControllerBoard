#pragma once

#include <cstddef>
#include <cstdint>

#include "IHalfDuplexUart.h"

/**
 * UART1 on one GPIO, 8 data bits, even parity, two stop bits.
 * TX and RX share the pin. Outbound bytes are push-pull. The pad
 * floats while a reply byte is read.
 */
class Esp32HalfDuplexUart : public IHalfDuplexUart
{
public:
  /**
   * Creates a UART with no driver and no pad.
   */
  Esp32HalfDuplexUart();

  /**
   * Installs UART1 at baud and leaves the pad alone, so a GPIO break
   * can run before claim().
   *
   * @param gpio Pin number. The same pin is TX and RX once claimed.
   * @param baud Bit rate.
   * @return Nothing.
   */
  void install(uint8_t gpio, uint32_t baud) override;

  /**
   * Connects UART1 TX and RX. The idle level is high. No driver
   * install runs here, so this returns before the UPDI SYNCH window
   * closes.
   *
   * @return Nothing.
   */
  void claim() override;

  /**
   * Deletes the UART driver and releases the pin matrix.
   *
   * @return Nothing.
   */
  void detach() override;

  /**
   * Reports bytes waiting in the UART FIFO.
   *
   * @return Number of available bytes.
   */
  int available() const override;

  /**
   * Reads one byte. A buffered byte keeps the transmitter connected.
   * An empty FIFO disconnects it and floats the pad for the wait,
   * then connects the transmitter and the pull-up again.
   *
   * @return Byte value, or -1 on timeout.
   */
  int read() override;

  /**
   * Clears the low sample from reply waits.
   *
   * @return Nothing.
   */
  void clearListen() override;

  /**
   * Reports whether a reply wait sampled the pin low.
   *
   * @return True when any wait since clearListen() sampled low.
   */
  bool listenWentLow() const override;

  /**
   * Reports when the released wait first saw the pad low.
   *
   * @return Microseconds from the start of that wait. Zero when already low
   * or when the pad stayed high.
   */
  uint16_t listenPulseAtUs() const override;

  /**
   * Reports the width of that first low.
   *
   * @return Microseconds, saturated at 65535.
   */
  uint16_t listenPulseWidthUs() const override;

  /**
   * Reports a UART fault latched during the released wait.
   *
   * @return None, or break ahead of frame, parity, and glitch.
   */
  UartListenFault listenFault() const override;

  /**
   * Reports how many lows this wait sampled.
   *
   * @return The count, saturated at 255.
   */
  uint8_t listenEdgeCount() const override;

  /**
   * Reports one of the first stored low widths.
   *
   * @param index Zero-based width.
   * @return Microseconds, or 0 when that slot was not filled.
   */
  uint16_t listenWidthUs(uint8_t index) const override;

  /**
   * Drives the bytes push-pull, waits until the shifter is empty, then
   * releases the pad. Echoes are already in the FIFO.
   *
   * @param data Bytes to write.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void write(const uint8_t* data, size_t length) override;

private:
  uint8_t _gpio = 0;
  bool _installed = false;
  bool _claimed = false;
  bool _haveGpio = false;
  bool _listenWentLow = false;
  uint16_t _pulseAtUs = 0;
  uint16_t _pulseUs = 0;
  UartListenFault _listenFault = UartListenFault::None;
  uint8_t _edgeCount = 0;
  uint16_t _widthUs[kUartListenWidths] = {};
  bool _pulseStarted = false;
  bool _pulseClosed = false;
  bool _inLow = false;
  uint32_t _listenOriginUs = 0;
  uint32_t _fellAtUs = 0;

  /**
   * Drives UART TX push-pull. Output is enabled only after the UART
   * signal is routed, and the idle level is high, so the pad does not
   * fall during the change.
   *
   * @return Nothing.
   */
  void _driveTransmitter();

  /**
   * Removes UART TX from the pad. RX stays. The pull-up stays only
   * when keepPullUp is true. A reply wait passes false so a short
   * target pulse is not pulled high before the UART samples it.
   *
   * @param keepPullUp True to leave the pull-up enabled.
   * @return Nothing.
   */
  void _releaseTransmitter(bool keepPullUp);

  /**
   * Connects UART TX to the pad again. Open-drain and the pull-up stay.
   *
   * @return Nothing.
   */
  void _connectTransmitter();

  /**
   * Samples the pad while waiting for one byte. Records the first low
   * and any UART fault flag.
   *
   * @return Byte value, or -1 when the wait ends with none.
   */
  int _listenForByte();

  /**
   * Clears the low, the pulse times, and the UART fault.
   *
   * @return Nothing.
   */
  void _clearListenRecord();

  /**
   * Times one pad sample against the start of this wait.
   *
   * @param isLow True when the pad reads low.
   * @return Nothing.
   */
  void _noteListenLevel(bool isLow);

  /**
   * Keeps the highest-priority UART fault currently latched.
   * Break outranks frame, then parity, then glitch.
   *
   * @return Nothing.
   */
  void _noteListenFault();
};

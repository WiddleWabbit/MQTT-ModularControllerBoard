#pragma once

#include <cstddef>
#include <cstdint>

/**
 * How many low widths a released wait keeps. Later lows still increment
 * the edge count.
 */
static const uint8_t kUartListenWidths = 4;

/**
 * Why a released wait saw the wire move and still returned no byte.
 * Several flags at once keep break, then frame, then parity, then glitch.
 */
enum class UartListenFault : uint8_t
{
  None = 0,
  Frame = 1,
  Parity = 2,
  Break = 3,
  Glitch = 4
};

/**
 * Single-wire UART. TX and RX share one GPIO. The wire loops TX back
 * into RX. The caller reads the echoes of one write before a target byte.
 */
class IHalfDuplexUart
{
public:
  virtual ~IHalfDuplexUart() = default;

  /**
   * Starts the UART at baud without connecting the pad. The caller
   * can bit-bang that pad until claim(). A second call replaces the
   * driver and leaves the pad disconnected.
   *
   * @param gpio Pin number. The same pin is TX and RX once claimed.
   * @param baud Bit rate.
   * @return Nothing.
   */
  virtual void install(uint8_t gpio, uint32_t baud) = 0;

  /**
   * Connects TX and RX to the installed GPIO. Does nothing when the
   * driver is absent or the pad is already connected.
   *
   * @return Nothing.
   */
  virtual void claim() = 0;

  /**
   * Stops the UART and releases the pin matrix.
   *
   * @return Nothing.
   */
  virtual void detach() = 0;

  /**
   * Reports how many bytes can be read.
   *
   * @return Number of available bytes.
   */
  virtual int available() const = 0;

  /**
   * Reads one byte. A byte already buffered is returned with the
   * transmitter still connected. When none is buffered, the transmitter
   * is disconnected and the pull-up is removed for the wait, then both
   * are restored.
   *
   * @return Byte value, or -1 when none arrives.
   */
  virtual int read() = 0;

  /**
   * Clears the low sample from the reply waits.
   *
   * @return Nothing.
   */
  virtual void clearListen() = 0;

  /**
   * Reports whether an empty-FIFO read saw the pin low.
   *
   * @return True when any wait since clearListen() sampled low.
   */
  virtual bool listenWentLow() const = 0;

  /**
   * Reports how long after the released wait started the first low began.
   * Zero means the pin was already low.
   *
   * @return Microseconds, saturated at 65535. Zero when the pin stayed high.
   */
  virtual uint16_t listenPulseAtUs() const = 0;

  /**
   * Reports how long that first low stayed down.
   *
   * @return Microseconds, saturated at 65535. Zero when the pin stayed high.
   */
  virtual uint16_t listenPulseWidthUs() const = 0;

  /**
   * Reports the UART receiver flag seen during the released wait.
   *
   * @return None when the receiver raised no frame, parity, break, or glitch flag.
   */
  virtual UartListenFault listenFault() const = 0;

  /**
   * Reports how many separate lows the released wait sampled.
   *
   * @return The count, saturated at 255.
   */
  virtual uint8_t listenEdgeCount() const = 0;

  /**
   * Reports one stored low width. Only the first kUartListenWidths lows
   * are kept.
   *
   * @param index Zero-based width. Indexes past the stored set return 0.
   * @return Microseconds, saturated at 65535.
   */
  virtual uint16_t listenWidthUs(uint8_t index) const = 0;

  /**
   * Writes raw bytes.
   *
   * @param data Bytes to write. Ignored when null or length is 0.
   * @param length Number of bytes.
   * @return Nothing.
   */
  virtual void write(const uint8_t* data, size_t length) = 0;
};

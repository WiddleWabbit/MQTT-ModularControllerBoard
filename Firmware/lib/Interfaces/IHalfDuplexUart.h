#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Single-wire UART. TX and RX share one GPIO. The driver owns echo
 * removal only in the sense that the wire loops TX back into RX;
 * the caller reads that echo before the next byte.
 */
class IHalfDuplexUart
{
public:
  virtual ~IHalfDuplexUart() = default;

  /**
   * Claims the GPIO and starts the UART at baud.
   *
   * @param gpio Pin number. The same pin is TX and RX.
   * @param baud Bit rate.
   * @return Nothing.
   */
  virtual void attach(uint8_t gpio, uint32_t baud) = 0;

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
   * Reads one byte.
   *
   * @return Byte value, or -1 when none arrives.
   */
  virtual int read() = 0;

  /**
   * Writes raw bytes.
   *
   * @param data Bytes to write. Ignored when null or length is 0.
   * @param length Number of bytes.
   * @return Nothing.
   */
  virtual void write(const uint8_t* data, size_t length) = 0;
};

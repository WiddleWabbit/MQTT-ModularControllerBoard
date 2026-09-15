#pragma once

#include <cstddef>

/**
 * Abstracts a byte-oriented serial port.
 */
class ISerialPort
{
public:
  virtual ~ISerialPort() = default;

  /**
   * Reports whether the USB serial link is physically plugged in.
   *
   * @return True when the USB serial link is present.
   */
  virtual bool isPlugged() const = 0;

  /**
   * Reports the number of bytes ready to read.
   *
   * @return Number of available bytes.
   */
  virtual size_t available() const = 0;

  /**
   * Reads one byte.
   *
   * @return Byte value, or -1 when no byte is available.
   */
  virtual int read() = 0;

  /**
   * Writes a complete line.
   *
   * @param line Text without a required line terminator.
   * @return Nothing.
   */
  virtual void writeLine(const char* line) = 0;
};

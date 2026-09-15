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

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Raw byte port. Programming traffic is binary, so it does not use lines.
 */
class IBytePort
{
public:
  virtual ~IBytePort() = default;

  /**
   * Reports whether the link is present.
   *
   * @return True when the port is plugged in.
   */
  virtual bool isPlugged() const = 0;

  /**
   * Reports how many bytes can be read.
   *
   * @return Number of available bytes.
   */
  virtual size_t available() const = 0;

  /**
   * Reads one byte.
   *
   * @return Byte value, or -1 when none is available.
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

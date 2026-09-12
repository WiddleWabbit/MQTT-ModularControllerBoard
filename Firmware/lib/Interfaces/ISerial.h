#pragma once

#include <stddef.h>

/**
 * Abstracts formatted serial-console output from Arduino and application logic.
 *
 * Implementations must preserve call order.  A null string is treated as an
 * empty string, while println always appends one line ending.
 */
class ISerial {
public:
  /**
   * Releases the interface without owning a concrete serial port.
   *
   * @return Nothing.
   */
  virtual ~ISerial() = default;

  /**
   * Initializes the serial port.
   *
   * @param baudRate Communication speed in bits per second.
   * @return Nothing.
   */
  virtual void begin(unsigned long baudRate) = 0;

  /**
   * Reports whether output can currently be sent.
   *
   * @return True when the serial endpoint is available; false otherwise.
   */
  virtual bool isConnected() const = 0;

  /**
   * Writes a string without a line ending.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters written.
   */
  virtual size_t print(const char* value) = 0;

  /**
   * Writes a signed integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(int value) = 0;

  /**
   * Writes an unsigned integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(unsigned int value) = 0;

  /**
   * Writes a signed long integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(long value) = 0;

  /**
   * Writes an unsigned long integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(unsigned long value) = 0;

  /**
   * Writes a boolean without a line ending.
   *
   * @param value Value to write as 1 or 0.
   * @return Number of characters written.
   */
  virtual size_t print(bool value) = 0;

  /**
   * Writes a floating-point value with two decimal places.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(float value) = 0;

  /**
   * Writes a double-precision value with two decimal places.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t print(double value) = 0;

  /**
   * Writes a line ending.
   *
   * @return Number of characters written for the line ending.
   */
  virtual size_t println() = 0;

  /**
   * Writes a string followed by a line ending.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters written.
   */
  virtual size_t println(const char* value) = 0;

  /**
   * Writes a signed integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t println(int value) = 0;

  /**
   * Writes an unsigned integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t println(unsigned int value) = 0;

  /**
   * Writes a signed long integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t println(long value) = 0;

  /**
   * Writes an unsigned long integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  virtual size_t println(unsigned long value) = 0;

  /**
   * Writes a boolean followed by a line ending.
   *
   * @param value Value to write as 1 or 0.
   * @return Number of characters written.
   */
  virtual size_t println(bool value) = 0;

  /**
   * Writes a floating-point value followed by a line ending.
   *
   * @param value Value to write with two decimal places.
   * @return Number of characters written.
   */
  virtual size_t println(float value) = 0;

  /**
   * Writes a double-precision value followed by a line ending.
   *
   * @param value Value to write with two decimal places.
   * @return Number of characters written.
   */
  virtual size_t println(double value) = 0;

  /**
   * Writes printf-style formatted text.
   *
   * @param format Null-terminated format string; nullptr is ignored.
   * @param ... Values referenced by the format string.
   * @return Number of characters formatted, or zero for a null format.
   */
  virtual int printf(const char* format, ...) = 0;
};

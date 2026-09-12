#pragma once

#include "ISerial.h"

class Esp32Serial : public ISerial {
public:
  /**
   * Initializes the ESP32 Arduino USB serial console.
   *
   * @param baudRate Communication speed in bits per second.
   * @return Nothing.
   */
  void begin(unsigned long baudRate) override;

  /**
   * Reports whether the USB serial endpoint has a connected host.
   *
   * @return True when output can be sent without waiting for a host.
   */
  bool isConnected() const override;

  /**
   * Writes a string without a line ending.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters written.
   */
  size_t print(const char* value) override;

  /**
   * Writes a signed integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(int value) override;

  /**
   * Writes an unsigned integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(unsigned int value) override;

  /**
   * Writes a signed long integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(long value) override;

  /**
   * Writes an unsigned long integer without a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(unsigned long value) override;

  /**
   * Writes a boolean without a line ending.
   *
   * @param value Value to write as 1 or 0.
   * @return Number of characters written.
   */
  size_t print(bool value) override;

  /**
   * Writes a floating-point value with two decimal places.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(float value) override;

  /**
   * Writes a double-precision value with two decimal places.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t print(double value) override;

  /**
   * Writes a line ending.
   *
   * @return Number of characters written for the line ending.
   */
  size_t println() override;

  /**
   * Writes a string followed by a line ending.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters written.
   */
  size_t println(const char* value) override;

  /**
   * Writes a signed integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t println(int value) override;

  /**
   * Writes an unsigned integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t println(unsigned int value) override;

  /**
   * Writes a signed long integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t println(long value) override;

  /**
   * Writes an unsigned long integer followed by a line ending.
   *
   * @param value Value to write.
   * @return Number of characters written.
   */
  size_t println(unsigned long value) override;

  /**
   * Writes a boolean followed by a line ending.
   *
   * @param value Value to write as 1 or 0.
   * @return Number of characters written.
   */
  size_t println(bool value) override;

  /**
   * Writes a floating-point value followed by a line ending.
   *
   * @param value Value to write with two decimal places.
   * @return Number of characters written.
   */
  size_t println(float value) override;

  /**
   * Writes a double-precision value followed by a line ending.
   *
   * @param value Value to write with two decimal places.
   * @return Number of characters written.
   */
  size_t println(double value) override;

  /**
   * Writes printf-style text to the ESP32 Arduino USB serial console.
   *
   * @param format Null-terminated format string; nullptr is ignored.
   * @param ... Values referenced by the format string.
   * @return Number of characters formatted, or zero for a null format.
   */
  int printf(const char* format, ...) override;
};

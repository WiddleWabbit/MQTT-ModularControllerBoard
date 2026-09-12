#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "ISerial.h"

class FakeSerial : public ISerial {
public:
  /**
   * Records serial initialization without requiring a hardware port.
   *
   * @param baudRate Communication speed requested by the caller.
   * @return Nothing.
   */
  void begin(unsigned long baudRate) override
  {
    ++beginCallCount;
    lastBaudRate = baudRate;
  }

  /**
   * Reports whether the fake endpoint is configured to accept output.
   *
   * @return True when output is enabled.
   */
  bool isConnected() const override
  {
    return connected;
  }

  /**
   * Appends text to the inspectable fake output.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters appended.
   */
  size_t print(const char* value) override
  {
    if (!connected) {
      return 0;
    }
    ++writeCallCount;
    return append(value == nullptr ? "" : value);
  }

  /**
   * Appends a signed integer to the inspectable fake output.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(int value) override { return connected ? appendFormatted("%d", value) : 0; }

  /**
   * Appends an unsigned integer to the inspectable fake output.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(unsigned int value) override
  {
    return connected ? appendFormatted("%u", value) : 0;
  }

  /**
   * Appends a signed long integer to the inspectable fake output.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(long value) override { return connected ? appendFormatted("%ld", value) : 0; }

  /**
   * Appends an unsigned long integer to the inspectable fake output.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(unsigned long value) override
  {
    return connected ? appendFormatted("%lu", value) : 0;
  }

  /**
   * Appends a boolean as 1 or 0 to the inspectable fake output.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(bool value) override
  {
    return connected ? appendFormatted("%d", value ? 1 : 0) : 0;
  }

  /**
   * Appends a float with two decimal places.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(float value) override
  {
    return connected ? appendFormatted("%.2f", static_cast<double>(value)) : 0;
  }

  /**
   * Appends a double with two decimal places.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t print(double value) override
  {
    return connected ? appendFormatted("%.2f", value) : 0;
  }

  /**
   * Appends a line ending to the inspectable fake output.
   *
   * @return Number of characters appended.
   */
  size_t println() override
  {
    if (!connected) {
      return 0;
    }
    ++writeCallCount;
    return append("\n");
  }

  /**
   * Appends text and a line ending.
   *
   * @param value Null-terminated text; nullptr is treated as empty text.
   * @return Number of characters appended.
   */
  size_t println(const char* value) override
  {
    if (!connected) {
      return 0;
    }
    ++writeCallCount;
    return append(value == nullptr ? "\n" : std::string(value) + "\n");
  }

  /**
   * Appends a signed integer and a line ending.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t println(int value) override
  {
    return connected ? appendFormattedWithNewline("%d", value) : 0;
  }

  /**
   * Appends an unsigned integer and a line ending.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t println(unsigned int value) override
  {
    return connected ? appendFormattedWithNewline("%u", value) : 0;
  }

  /**
   * Appends a signed long integer and a line ending.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t println(long value) override
  {
    return connected ? appendFormattedWithNewline("%ld", value) : 0;
  }

  /**
   * Appends an unsigned long integer and a line ending.
   *
   * @param value Value to append.
   * @return Number of characters appended.
   */
  size_t println(unsigned long value) override
  {
    return connected ? appendFormattedWithNewline("%lu", value) : 0;
  }

  /**
   * Appends a boolean and a line ending.
   *
   * @param value Value to append as 1 or 0.
   * @return Number of characters appended.
   */
  size_t println(bool value) override
  {
    return connected ? appendFormattedWithNewline("%d", value ? 1 : 0) : 0;
  }

  /**
   * Appends a float and a line ending.
   *
   * @param value Value to append with two decimal places.
   * @return Number of characters appended.
   */
  size_t println(float value) override
  {
    return connected ? appendFormattedWithNewline("%.2f", static_cast<double>(value)) : 0;
  }

  /**
   * Appends a double and a line ending.
   *
   * @param value Value to append with two decimal places.
   * @return Number of characters appended.
   */
  size_t println(double value) override
  {
    return connected ? appendFormattedWithNewline("%.2f", value) : 0;
  }

  /**
   * Appends printf-style formatted text to the fake output.
   *
   * @param format Null-terminated format string; nullptr is ignored.
   * @param ... Values referenced by the format string.
   * @return Number of characters formatted, or zero for a null format.
   */
  int printf(const char* format, ...) override
  {
    if (format == nullptr || !connected) {
      return 0;
    }

    va_list arguments;
    va_start(arguments, format);
    const int result = appendVFormatted(format, arguments);
    va_end(arguments);
    return result;
  }

  std::string output;
  unsigned int beginCallCount = 0;
  unsigned int writeCallCount = 0;
  unsigned long lastBaudRate = 0;
  bool connected = true;

private:
  /**
   * Appends already formatted text and returns its length.
   *
   * @param value Text to append.
   * @return Number of characters appended.
   */
  size_t append(const char* value)
  {
    output += value;
    return std::char_traits<char>::length(value);
  }

  /**
   * Appends an owned string and returns its length.
   *
   * @param value Text to append.
   * @return Number of characters appended.
   */
  size_t append(const std::string& value)
  {
    output += value;
    return value.length();
  }

  /**
   * Formats one value and appends it as a single write.
   *
   * @param format printf-style format.
   * @param value Value referenced by the format.
   * @return Number of characters appended.
   */
  template <typename ValueType>
  size_t appendFormatted(const char* format, ValueType value)
  {
    ++writeCallCount;
    char buffer[64];
    const int length = std::snprintf(buffer, sizeof(buffer), format, value);
    if (length <= 0) {
      return 0;
    }
    return append(std::string(buffer, static_cast<size_t>(length)));
  }

  /**
   * Formats one value with a line ending and appends it as one write.
   *
   * @param format printf-style format.
   * @param value Value referenced by the format.
   * @return Number of characters appended.
   */
  template <typename ValueType>
  size_t appendFormattedWithNewline(const char* format, ValueType value)
  {
    ++writeCallCount;
    char buffer[64];
    const int length = std::snprintf(buffer, sizeof(buffer), format, value);
    if (length <= 0) {
      return append("\n");
    }
    return append(std::string(buffer, static_cast<size_t>(length)) + "\n");
  }

  /**
   * Formats a variadic argument list and appends the complete result.
   *
   * @param format printf-style format.
   * @param arguments Arguments referenced by the format.
   * @return Number of characters formatted, or a negative error.
   */
  int appendVFormatted(const char* format, va_list arguments)
  {
    va_list sizeArguments;
    va_copy(sizeArguments, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, sizeArguments);
    va_end(sizeArguments);
    if (length < 0) {
      return length;
    }

    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    va_list writeArguments;
    va_copy(writeArguments, arguments);
    std::vsnprintf(buffer.data(), buffer.size(), format, writeArguments);
    va_end(writeArguments);
    ++writeCallCount;
    append(buffer.data());
    return length;
  }
};

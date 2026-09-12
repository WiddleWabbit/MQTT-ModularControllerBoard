#include "Esp32Serial.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <cstdarg>
#include <cstdlib>

// ========== Initialization ==========

void Esp32Serial::begin(unsigned long baudRate)
{
  Serial.begin(baudRate);
}

bool Esp32Serial::isConnected() const
{
  return static_cast<bool>(Serial);
}


// ========== Print Operations ==========

size_t Esp32Serial::print(const char* value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value == nullptr ? "" : value);
}

size_t Esp32Serial::print(int value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(unsigned int value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(long value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(unsigned long value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(bool value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(float value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}

size_t Esp32Serial::print(double value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.print(value);
}


// ========== Line Print Operations ==========

size_t Esp32Serial::println()
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println();
}

size_t Esp32Serial::println(const char* value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value == nullptr ? "" : value);
}

size_t Esp32Serial::println(int value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(unsigned int value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(long value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(unsigned long value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(bool value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(float value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}

size_t Esp32Serial::println(double value)
{
  if (!isConnected()) {
    return 0;
  }
  return Serial.println(value);
}


// ========== Formatted Output ==========

int Esp32Serial::printf(const char* format, ...)
{
  if (format == nullptr || !isConnected()) {
    return 0;
  }

  va_list arguments;
  va_start(arguments, format);
  va_list sizeArguments;
  va_copy(sizeArguments, arguments);
  const int length = vsnprintf(nullptr, 0, format, sizeArguments);
  va_end(sizeArguments);

  if (length < 0) {
    va_end(arguments);
    return length;
  }

  char* buffer = static_cast<char*>(malloc(static_cast<size_t>(length) + 1));
  if (buffer == nullptr) {
    va_end(arguments);
    return -1;
  }

  const int formattedLength = vsnprintf(buffer,
                                        static_cast<size_t>(length) + 1,
                                        format,
                                        arguments);
  va_end(arguments);

  if (formattedLength >= 0) {
    Serial.print(buffer);
  }
  free(buffer);
  return formattedLength;
}

#endif

#include "Esp32SerialPort.h"

Esp32SerialPort::Esp32SerialPort(HWCDC& serial)
  : _serial(serial)
{
}

bool Esp32SerialPort::isPlugged() const
{
  return _serial.isPlugged();
}

size_t Esp32SerialPort::available() const
{
  return _serial.available();
}

int Esp32SerialPort::read()
{
  return _serial.read();
}

/**
 * Writes one line and a newline.
 *
 * @param line Text without a required line terminator.
 * @return Nothing.
 */
void Esp32SerialPort::writeLine(const char* line)
{
  _serial.println(line == nullptr ? "" : line);
}

/**
 * Writes raw bytes for the programming session.
 *
 * @param data Bytes to write. Ignored when null or length is 0.
 * @param length Number of bytes.
 * @return Nothing.
 */
void Esp32SerialPort::write(const uint8_t* data, size_t length)
{
  if (data == nullptr || length == 0)
  {
    return;
  }
  _serial.write(data, length);
}

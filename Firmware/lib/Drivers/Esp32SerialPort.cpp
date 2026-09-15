#include "Esp32SerialPort.h"

Esp32SerialPort::Esp32SerialPort(Stream& serial)
  : _serial(serial)
{
}

size_t Esp32SerialPort::available() const
{
  return _serial.available();
}

int Esp32SerialPort::read()
{
  return _serial.read();
}

void Esp32SerialPort::writeLine(const char* line)
{
  _serial.println(line == nullptr ? "" : line);
}

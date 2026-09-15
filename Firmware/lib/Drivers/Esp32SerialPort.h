#pragma once

#include <Arduino.h>

#include "ISerialPort.h"

/**
 * Adapts Arduino Serial to the serial-port interface.
 */
class Esp32SerialPort : public ISerialPort
{
public:
  explicit Esp32SerialPort(Stream& serial);
  size_t available() const override;
  int read() override;
  void writeLine(const char* line) override;

private:
  Stream& _serial;
};

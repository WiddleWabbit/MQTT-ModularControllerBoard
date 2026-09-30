#pragma once

#include <Arduino.h>
#include <HWCDC.h>

#include "IBytePort.h"
#include "ISerialPort.h"

/**
 * Adapts Arduino Serial to a line port and a raw byte port.
 * One USB CDC link serves the console and the STK500 session.
 */
class Esp32SerialPort : public ISerialPort, public IBytePort
{
public:
  /**
   * Wraps the hardware USB serial port.
   *
   * @param serial Arduino HWCDC port, normally Serial.
   */
  explicit Esp32SerialPort(HWCDC& serial);

  /**
   * Reports whether the USB serial link is physically plugged in.
   *
   * @return True when the link is present.
   */
  bool isPlugged() const override;

  /**
   * Reports how many bytes can be read.
   *
   * @return Number of available bytes.
   */
  size_t available() const override;

  /**
   * Reads one byte.
   *
   * @return Byte value, or -1 when none is available.
   */
  int read() override;

  /**
   * Writes one line and a newline.
   *
   * @param line Text without a required line terminator.
   * @return Nothing.
   */
  void writeLine(const char* line) override;

  /**
   * Writes raw bytes for the programming session.
   *
   * @param data Bytes to write. Ignored when null or length is 0.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void write(const uint8_t* data, size_t length) override;

private:
  HWCDC& _serial;
};

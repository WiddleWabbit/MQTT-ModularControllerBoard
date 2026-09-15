#pragma once

#include <cstdint>

#include "IUsbVbus.h"

/**
 * Reads USB VBUS presence from a digital ESP32 input.
 */
class Esp32UsbVbus : public IUsbVbus
{
public:
  /**
   * Creates a VBUS reader and configures its input pin.
   *
   * @param pin VBUS sense GPIO.
   */
  explicit Esp32UsbVbus(uint8_t pin);

  bool isPresent() const override;

private:
  uint8_t _pin;
};

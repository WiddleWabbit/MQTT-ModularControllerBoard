#include "Esp32UsbVbus.h"

#include <Arduino.h>

Esp32UsbVbus::Esp32UsbVbus(uint8_t pin)
  : _pin(pin)
{
  pinMode(_pin, INPUT);
}

bool Esp32UsbVbus::isPresent() const
{
  return digitalRead(_pin) == HIGH;
}

#include "Esp32DigitalPin.h"

#include <Arduino.h>

// ========== Construction ==========

Esp32DigitalPin::Esp32DigitalPin(uint8_t gpio) : _gpio(gpio)
{
}


// ========== Public API ==========

/**
 * Sets electrical mode via Arduino pinMode. Open-drain defaults to LOW.
 *
 * @param mode Requested electrical mode.
 * @return Nothing.
 */
void Esp32DigitalPin::setMode(PinMode mode)
{
  switch (mode)
  {
    case PinMode::DigitalInputPullup:
      pinMode(_gpio, INPUT_PULLUP);
      break;
    case PinMode::DigitalOutput:
      pinMode(_gpio, OUTPUT);
      break;
    case PinMode::DigitalOutputOpenDrain:
      pinMode(_gpio, OUTPUT_OPEN_DRAIN);
      digitalWrite(_gpio, LOW);
      break;
    case PinMode::DigitalInput:
    default:
      pinMode(_gpio, INPUT);
      break;
  }
}

/**
 * Reads the GPIO level.
 *
 * @return True when HIGH.
 */
bool Esp32DigitalPin::read() const
{
  return digitalRead(_gpio) != LOW;
}

/**
 * Writes the GPIO level when configured as an output.
 *
 * @param level True for HIGH / open-drain Hi-Z.
 * @return Nothing.
 */
void Esp32DigitalPin::write(bool level)
{
  digitalWrite(_gpio, level ? HIGH : LOW);
}

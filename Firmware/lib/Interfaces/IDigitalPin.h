#pragma once

#include <cstdint>

/**
 * Pin modes. Names avoid Arduino.h macros INPUT, OUTPUT, INPUT_PULLUP.
 */
enum class PinMode : uint8_t
{
  DigitalInput,
  DigitalInputPullup,
  DigitalOutput,
  DigitalOutputOpenDrain
};

/**
 * Abstracts one GPIO line with runtime-selectable electrical mode.
 */
class IDigitalPin
{
public:
  virtual ~IDigitalPin() = default;

  /**
   * Sets electrical mode. For DigitalOutputOpenDrain the ESP32 driver
   * detaches any prior peripheral and drives LOW as the safe default.
   *
   * @param mode Requested electrical mode.
   * @return Nothing.
   */
  virtual void setMode(PinMode mode) = 0;

  /**
   * Reads the pin level.
   *
   * @return True when the pin is HIGH.
   */
  virtual bool read() const = 0;

  /**
   * Drives the pin when the mode is an output. True is HIGH / open-drain
   * Hi-Z. Ignored in input modes.
   *
   * @param level True for HIGH, false for LOW.
   * @return Nothing.
   */
  virtual void write(bool level) = 0;
};

#pragma once

#include <cstdint>

#include "IDigitalPin.h"

/**
 * Adapts one ESP32 GPIO to IDigitalPin using Arduino pinMode.
 */
class Esp32DigitalPin : public IDigitalPin
{
public:
  /**
   * Creates a pin adapter for one GPIO number.
   *
   * @param gpio ESP32 GPIO number.
   */
  explicit Esp32DigitalPin(uint8_t gpio);

  /**
   * Sets electrical mode via Arduino pinMode. Open-drain defaults to LOW.
   *
   * @param mode Requested electrical mode.
   * @return Nothing.
   */
  void setMode(PinMode mode) override;

  /**
   * Reads the GPIO level.
   *
   * @return True when HIGH.
   */
  bool read() const override;

  /**
   * Writes the GPIO level when configured as an output.
   *
   * @param level True for HIGH / open-drain Hi-Z.
   * @return Nothing.
   */
  void write(bool level) override;

private:
  uint8_t _gpio;
};

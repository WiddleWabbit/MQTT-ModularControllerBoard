#pragma once

#include "IDigitalPin.h"

class FakeDigitalPin : public IDigitalPin
{
public:
  /**
   * Sets electrical mode. Open-drain defaults to LOW.
   *
   * @param mode Requested electrical mode.
   * @return Nothing.
   */
  void setMode(PinMode mode) override
  {
    this->mode = mode;
    setModeCount++;
    if (mode == PinMode::DigitalOutput)
    {
      pushPullUsed = true;
    }
    if (mode == PinMode::DigitalOutputOpenDrain)
    {
      level = false;
    }
  }

  /**
   * Reads the pin. Input modes use externalLevel; outputs use level.
   *
   * @return True when HIGH.
   */
  bool read() const override
  {
    if (mode == PinMode::DigitalInput || mode == PinMode::DigitalInputPullup)
    {
      return externalLevel;
    }
    return level;
  }

  /**
   * Drives an output pin. Records push-pull HIGH attempts.
   *
   * @param value True for HIGH.
   * @return Nothing.
   */
  void write(bool value) override
  {
    writeCount++;
    lastWritten = value;
    if (mode == PinMode::DigitalOutput && value)
    {
      wroteHighWhilePushPull = true;
    }
    if (mode == PinMode::DigitalOutput ||
        mode == PinMode::DigitalOutputOpenDrain)
    {
      level = value;
    }
  }

  /**
   * Sets whether a module is pulling sense to ground.
   *
   * @param present True when the module is seated (sense LOW).
   * @return Nothing.
   */
  void setPresent(bool present)
  {
    externalLevel = !present;
  }

  PinMode mode = PinMode::DigitalInput;
  bool level = true;
  bool externalLevel = true;
  bool lastWritten = true;
  bool wroteHighWhilePushPull = false;
  bool pushPullUsed = false;
  int writeCount = 0;
  int setModeCount = 0;
};

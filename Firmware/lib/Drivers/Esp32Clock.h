#pragma once

#include "IClock.h"

/**
 * Adapts the ESP32 Arduino millisecond clock to IClock.
 */
class Esp32Clock : public IClock
{
public:
  /**
   * Reads the ESP32 monotonic millisecond counter.
   *
   * @return Milliseconds since boot.
   */
  uint32_t millis() const override;
};

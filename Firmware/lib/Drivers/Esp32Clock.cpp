#include "Esp32Clock.h"

#include <Arduino.h>

// ========== Public API ==========

/**
 * Reads the ESP32 monotonic millisecond counter.
 *
 * @return Milliseconds since boot.
 */
uint32_t Esp32Clock::millis() const
{
  return ::millis();
}

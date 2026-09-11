#include "Esp32Clock.h"

#ifdef ARDUINO

#include <Arduino.h>

unsigned long Esp32Clock::nowMillis() const
{
  return millis();
}

#endif

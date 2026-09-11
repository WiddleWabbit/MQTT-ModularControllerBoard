#include "Esp32SystemControl.h"

#ifdef ARDUINO

#include <Arduino.h>

void Esp32SystemControl::restart()
{
  ESP.restart();
}

#endif

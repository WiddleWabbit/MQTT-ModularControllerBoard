#pragma once

#include "IClock.h"

class Esp32Clock : public IClock {
public:
  /**
   * Returns the ESP32 Arduino millisecond counter.
   *
   * @return Current millisecond counter.
   */
  unsigned long nowMillis() const override;
};

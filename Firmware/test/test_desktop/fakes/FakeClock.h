#pragma once

#include "IClock.h"

class FakeClock : public IClock {
public:
  /**
   * Returns the simulated millisecond counter.
   *
   * @return Current fake time.
   */
  unsigned long nowMillis() const override
  {
    return currentMillis;
  }

  /**
   * Advances simulated time without sleeping the desktop test.
   *
   * @param milliseconds Amount of simulated elapsed time.
   * @return Nothing.
   */
  void advance(unsigned long milliseconds)
  {
    currentMillis += milliseconds;
  }

  unsigned long currentMillis = 0;
};

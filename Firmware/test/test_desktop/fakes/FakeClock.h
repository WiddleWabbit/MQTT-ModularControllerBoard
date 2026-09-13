#pragma once

#include <cstdint>

#include "IClock.h"

class FakeClock : public IClock
{
public:
  /**
   * Returns the controllable monotonic time.
   *
   * @return Current time in milliseconds.
   */
  uint32_t millis() const override
  {
    return _millis;
  }

  /**
   * Advances the controllable monotonic time.
   *
   * @param milliseconds Amount of time to advance.
   * @return Nothing.
   */
  void advance(uint32_t milliseconds)
  {
    _millis += milliseconds;
  }

  /**
   * Sets the controllable monotonic time.
   *
   * @param milliseconds New millisecond value.
   * @return Nothing.
   */
  void set(uint32_t milliseconds)
  {
    _millis = milliseconds;
  }

private:
  uint32_t _millis = 0;
};

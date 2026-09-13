#pragma once

#include <cstdint>

/**
 * Provides monotonic elapsed time to timing-sensitive logic.
 */
class IClock
{
public:
  virtual ~IClock() = default;

  /**
   * Returns elapsed milliseconds since the clock started.
   *
   * @return Monotonic millisecond count.
   */
  virtual uint32_t millis() const = 0;
};

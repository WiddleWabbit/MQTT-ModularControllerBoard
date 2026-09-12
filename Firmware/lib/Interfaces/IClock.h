#pragma once

class IClock {
public:
  /**
   * Releases the interface without owning a concrete clock.
   *
   * @return Nothing.
   */
  virtual ~IClock() = default;

  /**
   * Returns the monotonic millisecond counter used for elapsed-time checks.
   *
   * @return Current millisecond counter.
   */
  virtual unsigned long nowMillis() const = 0;
};

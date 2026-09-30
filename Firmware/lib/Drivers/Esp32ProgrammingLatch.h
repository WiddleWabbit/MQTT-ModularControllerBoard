#pragma once

/**
 * Remembers that an ISP session should resume after a restart.
 * The marker is one word in the unloaded part of RTC slow memory
 * (.rtc_noinit). A USB-open reset or a software restart keeps it.
 * The reset button and a power cycle clear it.
 */
class Esp32ProgrammingLatch
{
public:
  /**
   * Reports whether the ISP session marker is stored.
   *
   * @return True when the marker matches.
   */
  bool isSet() const;

  /**
   * Stores the ISP session marker.
   *
   * @return Nothing.
   */
  void set();

  /**
   * Clears the marker so the next boot is a normal start.
   *
   * @return Nothing.
   */
  void clear();
};

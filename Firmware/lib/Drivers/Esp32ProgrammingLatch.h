#pragma once

/**
 * Remembers that an ISP session should resume after a software restart.
 * The flag lives in RTC memory: a USB-open reset keeps it, a power cycle
 * clears it.
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

#pragma once

#include <cstdint>

/**
 * Remembers a programming session across a restart.
 * The record is one uninitialized block in RTC slow memory
 * (.rtc_noinit). A USB-open reset or a software restart keeps it.
 * The reset button and a power cycle clear it. An older single-word
 * ISP marker does not match this record.
 */
class Esp32ProgrammingLatch
{
public:
  /**
   * Reports whether a slot and method are stored.
   *
   * @return True when the magic matches and the slot and method are valid.
   */
  bool isSet() const;

  /**
   * Stores the session so the next restart resumes it.
   *
   * @param slot Firmware slot, 1 through 4.
   * @param method 1 for ISP, 2 for UPDI.
   * @return Nothing.
   */
  void set(uint8_t slot, uint8_t method);

  /**
   * Reports the stored slot. Meaningful when isSet() is true.
   *
   * @return Slot number.
   */
  uint8_t slot() const;

  /**
   * Reports the stored method. 1 is ISP and 2 is UPDI.
   *
   * @return Method number.
   */
  uint8_t method() const;

  /**
   * Clears the record so the next boot is a normal start.
   *
   * @return Nothing.
   */
  void clear();
};

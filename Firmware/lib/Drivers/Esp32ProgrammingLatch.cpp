#include "Esp32ProgrammingLatch.h"

#include <Arduino.h>

namespace
{
const uint32_t kProgrammingLatchMagic = 0x50524732UL;

struct ProgrammingLatchRecord
{
  uint32_t magic;
  uint8_t slot;
  uint8_t method;
  uint8_t reserved0;
  uint8_t reserved1;
};

// No initializer: .rtc_noinit is left as it was across a restart.
RTC_NOINIT_ATTR ProgrammingLatchRecord programmingLatchStorage;
}


// ========== Public API ==========

/**
 * Reports whether a slot and method are stored. The old ISP word
 * 0x49535031 does not match this magic.
 *
 * @return True when magic, slot, and method are all valid.
 */
bool Esp32ProgrammingLatch::isSet() const
{
  return programmingLatchStorage.magic == kProgrammingLatchMagic &&
         programmingLatchStorage.slot >= 1 &&
         programmingLatchStorage.slot <= 4 &&
         (programmingLatchStorage.method == 1 ||
          programmingLatchStorage.method == 2);
}

/**
 * Stores the session record.
 *
 * @param slot Firmware slot.
 * @param method 1 for ISP, 2 for UPDI.
 * @return Nothing.
 */
void Esp32ProgrammingLatch::set(uint8_t slot, uint8_t method)
{
  programmingLatchStorage.magic = kProgrammingLatchMagic;
  programmingLatchStorage.slot = slot;
  programmingLatchStorage.method = method;
  programmingLatchStorage.reserved0 = 0;
  programmingLatchStorage.reserved1 = 0;
}

/**
 * Reports the stored slot.
 *
 * @return Slot number.
 */
uint8_t Esp32ProgrammingLatch::slot() const
{
  return programmingLatchStorage.slot;
}

/**
 * Reports the stored method.
 *
 * @return 1 for ISP, 2 for UPDI.
 */
uint8_t Esp32ProgrammingLatch::method() const
{
  return programmingLatchStorage.method;
}

/**
 * Clears the record so the next boot is a normal start.
 *
 * @return Nothing.
 */
void Esp32ProgrammingLatch::clear()
{
  programmingLatchStorage.magic = 0;
  programmingLatchStorage.slot = 0;
  programmingLatchStorage.method = 0;
  programmingLatchStorage.reserved0 = 0;
  programmingLatchStorage.reserved1 = 0;
}

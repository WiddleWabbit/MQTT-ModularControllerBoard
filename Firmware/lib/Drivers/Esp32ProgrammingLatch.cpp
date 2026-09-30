#include "Esp32ProgrammingLatch.h"

#include <Arduino.h>

namespace
{
const uint32_t kProgrammingLatchMagic = 0x49535031UL;
RTC_DATA_ATTR uint32_t programmingLatchStorage = 0;
}


// ========== Public API ==========

/**
 * Reports whether the ISP session marker is stored.
 *
 * @return True when the marker matches.
 */
bool Esp32ProgrammingLatch::isSet() const
{
  return programmingLatchStorage == kProgrammingLatchMagic;
}

/**
 * Stores the ISP session marker.
 *
 * @return Nothing.
 */
void Esp32ProgrammingLatch::set()
{
  programmingLatchStorage = kProgrammingLatchMagic;
}

/**
 * Clears the marker so the next boot is a normal start.
 *
 * @return Nothing.
 */
void Esp32ProgrammingLatch::clear()
{
  programmingLatchStorage = 0;
}

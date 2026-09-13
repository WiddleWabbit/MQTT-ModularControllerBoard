#include "Esp32NtpAdapter.h"

#include <Arduino.h>
#include <time.h>

namespace
{
constexpr time_t minimumValidEpoch = 1700000000;
}

// ========== Public API ==========

/**
 * Configures ESP32 SNTP servers and timezone offsets.
 *
 * @param server1 Primary time server.
 * @param server2 Secondary time server.
 * @param server3 Tertiary time server.
 * @param utcOffsetSeconds UTC offset in seconds.
 * @param daylightOffsetSeconds Daylight-saving offset in seconds.
 * @return Nothing.
 */
void Esp32NtpAdapter::configure(const char* server1, const char* server2,
                                const char* server3,
                                int32_t utcOffsetSeconds,
                                int32_t daylightOffsetSeconds)
{
  configTime(utcOffsetSeconds, daylightOffsetSeconds, server1, server2, server3);
}

/**
 * Checks whether the system epoch is a plausible synchronized time.
 *
 * @return True when time is synchronized.
 */
bool Esp32NtpAdapter::isSynchronized() const
{
  return currentTime() >= minimumValidEpoch;
}

/**
 * Reads the ESP32 system epoch.
 *
 * @return Current time as seconds since the Unix epoch.
 */
time_t Esp32NtpAdapter::currentTime() const
{
  return time(nullptr);
}

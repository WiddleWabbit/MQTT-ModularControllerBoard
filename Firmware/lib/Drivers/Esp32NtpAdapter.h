#pragma once

#include "INtpAdapter.h"

/**
 * Adapts ESP32 Arduino configTime and system time to INtpAdapter.
 */
class Esp32NtpAdapter : public INtpAdapter
{
public:
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
  void configure(const char* server1, const char* server2, const char* server3,
                 int32_t utcOffsetSeconds,
                 int32_t daylightOffsetSeconds) override;

  /**
   * Checks whether the system epoch is a plausible synchronized time.
   *
   * @return True when time is synchronized.
   */
  bool isSynchronized() const override;

  /**
   * Reads the ESP32 system epoch.
   *
   * @return Current time as seconds since the Unix epoch.
   */
  time_t currentTime() const override;
};

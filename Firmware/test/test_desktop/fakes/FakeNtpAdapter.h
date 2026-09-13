#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "INtpAdapter.h"

class FakeNtpAdapter : public INtpAdapter
{
public:
  /**
   * Records NTP configuration.
   *
   * @param server1 Primary time server.
   * @param server2 Secondary time server.
   * @param server3 Tertiary time server.
   * @param utcOffsetSeconds UTC offset.
   * @param daylightOffsetSeconds Daylight-saving offset.
   * @return Nothing.
   */
  void configure(const char* server1, const char* server2, const char* server3,
                 int32_t utcOffsetSeconds,
                 int32_t daylightOffsetSeconds) override
  {
    configureCallCount++;
    lastServer1 = server1 == nullptr ? "" : server1;
    lastServer2 = server2 == nullptr ? "" : server2;
    lastServer3 = server3 == nullptr ? "" : server3;
    lastUtcOffsetSeconds = utcOffsetSeconds;
    lastDaylightOffsetSeconds = daylightOffsetSeconds;
  }

  /**
   * Reports whether simulated time synchronization has completed.
   *
   * @return True when synchronized.
   */
  bool isSynchronized() const override
  {
    return synchronized;
  }

  /**
   * Returns the simulated epoch.
   *
   * @return Current simulated epoch.
   */
  time_t currentTime() const override
  {
    return epoch;
  }

  bool synchronized = false;
  time_t epoch = 0;
  int configureCallCount = 0;
  std::string lastServer1;
  std::string lastServer2;
  std::string lastServer3;
  int32_t lastUtcOffsetSeconds = 0;
  int32_t lastDaylightOffsetSeconds = 0;
};

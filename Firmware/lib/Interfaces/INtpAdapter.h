#pragma once

#include <cstdint>
#include <ctime>

/**
 * Abstracts platform NTP configuration and synchronized time access.
 */
class INtpAdapter
{
public:
  virtual ~INtpAdapter() = default;

  /**
   * Configures the platform NTP client.
   *
   * @param server1 Primary time server.
   * @param server2 Secondary time server.
   * @param server3 Tertiary time server.
   * @param utcOffsetSeconds UTC offset in seconds.
   * @param daylightOffsetSeconds Daylight-saving offset in seconds.
   * @return Nothing.
   */
  virtual void configure(const char* server1, const char* server2,
                         const char* server3, int32_t utcOffsetSeconds,
                         int32_t daylightOffsetSeconds) = 0;

  /**
   * Reports whether a valid synchronized time is available.
   *
   * @return True when time is synchronized.
   */
  virtual bool isSynchronized() const = 0;

  /**
   * Reads the current synchronized epoch.
   *
   * @return Current time as seconds since the Unix epoch.
   */
  virtual time_t currentTime() const = 0;
};

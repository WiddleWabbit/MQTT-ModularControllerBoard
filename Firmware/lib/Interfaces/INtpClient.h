#pragma once

#include <ctime>

class INtpClient {
public:
  /**
   * Releases the interface without owning a concrete NTP client.
   *
   * @return Nothing.
   */
  virtual ~INtpClient() = default;

  /**
   * Starts an asynchronous synchronization request with an NTP server.
   *
   * @param serverName Host name of the NTP server.
   * @return Nothing.
   */
  virtual void requestSync(const char* serverName) = 0;

  /**
   * Advances the NTP client without blocking.
   *
   * @return Nothing.
   */
  virtual void update() = 0;

  /**
   * Reports whether a valid network time has been received.
   *
   * @return True when synchronized; otherwise false.
   */
  virtual bool isSynchronized() const = 0;

  /**
   * Returns the current synchronized Unix timestamp.
   *
   * @return Current time in seconds since 1970-01-01 UTC.
   */
  virtual time_t currentTime() const = 0;

  /**
   * Configures the POSIX timezone used for local-time conversion.
   *
   * @param timezone POSIX timezone specification.
   * @return True when accepted.
   */
  virtual bool setTimezone(const char* timezone) = 0;

  /**
   * Converts the current system time to local broken-down time.
   *
   * @param localTime Destination structure for local time fields.
   * @return True when conversion succeeds.
   */
  virtual bool getLocalTime(struct tm& localTime) const = 0;
};

#pragma once

#include "INtpClient.h"

class FakeNtpClient : public INtpClient {
public:
  /**
   * Records a synchronization request for test inspection.
   *
   * @param serverName NTP server supplied by the system under test.
   * @return Nothing.
   */
  void requestSync(const char* serverName) override
  {
    ++requestCount;
    lastServer = serverName;
  }

  /**
   * Records one non-blocking client update.
   *
   * @return Nothing.
   */
  void update() override
  {
    ++updateCount;
  }

  /**
   * Returns the controllable synchronization result.
   *
   * @return True when the fake is configured as synchronized.
   */
  bool isSynchronized() const override
  {
    return synchronized;
  }

  /**
   * Returns the controllable Unix timestamp.
   *
   * @return Current fake Unix timestamp.
   */
  time_t currentTime() const override
  {
    return unixTime;
  }

  bool setTimezone(const char* timezone) override
  {
    if (timezone == nullptr || timezone[0] == '\0') {
      return false;
    }

    timezoneName = timezone;
    return true;
  }

  bool getLocalTime(struct tm& localTime) const override
  {
    if (!synchronized || !hasLocalTime) {
      return false;
    }

    localTime = configuredLocalTime;
    return true;
  }

  /**
   * Restores the fake to its initial, unsynchronized state.
   *
   * @return Nothing.
   */
  void reset()
  {
    requestCount = 0;
    updateCount = 0;
    lastServer = "";
    synchronized = false;
    unixTime = 0;
    timezoneName = "";
    hasLocalTime = false;
    configuredLocalTime = {};
  }

  unsigned int requestCount = 0;
  unsigned int updateCount = 0;
  const char* lastServer = "";
  bool synchronized = false;
  time_t unixTime = 0;
  const char* timezoneName = "";
  bool hasLocalTime = false;
  struct tm configuredLocalTime = {};
};

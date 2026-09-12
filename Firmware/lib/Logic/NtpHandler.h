#pragma once

#include <string>

#include "IClock.h"
#include "INtpClient.h"
#include "IWifiStation.h"

class NtpHandler {
public:
  static constexpr unsigned long DefaultUpdateFrequencyMs = 1800000UL;

  /**
   * Creates an asynchronous NTP synchronization handler.
   *
   * @param wifi Station driver used to wait for network connectivity.
   * @param clock Monotonic clock used for periodic synchronization.
   * @param ntp NTP client that performs the platform-specific synchronization.
   * @param serverName Initial NTP server host name.
   * @param updateFrequencyMs Minimum interval between synchronization requests.
   */
  NtpHandler(IWifiStation& wifi, IClock& clock, INtpClient& ntp,
             const char* serverName = "pool.ntp.org",
             unsigned long updateFrequencyMs = DefaultUpdateFrequencyMs);

  /**
   * Advances WiFi-aware NTP synchronization without blocking.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Changes the NTP server host name.
   *
   * @param serverName Non-empty NTP server host name.
   * @return True when accepted; false when the name is null or empty.
   */
  bool setServer(const char* serverName);

  /**
   * Changes the minimum interval between synchronization requests.
   *
   * @param updateFrequencyMs Positive interval in milliseconds.
   * @return True when accepted; false when zero.
   */
  bool setUpdateFrequencyMs(unsigned long updateFrequencyMs);

  /**
   * Returns the configured NTP server host name.
   *
   * @return Null-terminated server host name.
   */
  const char* server() const;

  /**
   * Returns the configured synchronization interval.
   *
   * @return Interval in milliseconds.
   */
  unsigned long updateFrequencyMs() const;

  /**
   * Reports whether WiFi is connected and NTP time is synchronized.
   *
   * @return True only when both conditions are met.
   */
  bool isSynchronized() const;

private:
  IWifiStation& _wifi;
  IClock& _clock;
  INtpClient& _ntp;
  std::string _serverName;
  unsigned long _updateFrequencyMs;
  unsigned long _lastRequestAt = 0;
  bool _hasRequested = false;
  bool _wasConnected = false;
};

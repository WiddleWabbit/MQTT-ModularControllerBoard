#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "IClock.h"
#include "ISerialPort.h"
#include "NtpService.h"
#include "WifiManager.h"

/**
 * Writes USB-gated WiFi and NTP status snapshots on a fixed interval.
 */
class SerialStatusReporter
{
public:
  /**
   * Creates a status reporter using injected serial, clock, and services.
   *
   * @param serial Byte-oriented serial port.
   * @param clock Monotonic clock.
   * @param wifiManager WiFi connection manager.
   * @param ntpService NTP synchronization service.
   * @param intervalMs Snapshot interval in milliseconds.
   */
  SerialStatusReporter(ISerialPort& serial, IClock& clock,
                       WifiManager& wifiManager, NtpService& ntpService,
                       uint32_t intervalMs);

  /**
   * Writes one WiFi line and one NTP line when the USB link is plugged in
   * and the snapshot interval has elapsed.
   *
   * @return Nothing.
   */
  void update();

private:
  ISerialPort& _serial;
  IClock& _clock;
  WifiManager& _wifiManager;
  NtpService& _ntpService;
  uint32_t _intervalMs;
  uint32_t _lastReportAt = 0;

  /**
   * Writes the current WiFi manager state, including RSSI when connected.
   *
   * @return Nothing.
   */
  void _writeWifiStatus();

  /**
   * Writes the current NTP state, including local time when synchronized.
   *
   * @return Nothing.
   */
  void _writeNtpStatus();

  /**
   * Tests elapsed time using wrap-safe unsigned arithmetic.
   *
   * @param now Current monotonic time.
   * @param since Start time.
   * @param duration Required duration.
   * @return True when duration has elapsed.
   */
  static bool _hasElapsed(uint32_t now, uint32_t since, uint32_t duration);

  /**
   * Maps a WiFi manager state to its serial label.
   *
   * @param state WiFi manager state.
   * @return Status label.
   */
  static const char* _wifiStateName(WifiManagerState state);

  /**
   * Maps an NTP service state to its serial label.
   *
   * @param state NTP service state.
   * @return Status label.
   */
  static const char* _ntpStateName(NtpServiceState state);

  /**
   * Formats a UTC epoch plus timezone offsets as a local civil timestamp.
   *
   * @param utcEpoch Seconds since the Unix epoch.
   * @param utcOffsetSeconds UTC offset in seconds.
   * @param daylightOffsetSeconds Daylight-saving offset in seconds.
   * @param buffer Destination buffer.
   * @param bufferSize Destination capacity.
   * @return Nothing.
   */
  static void _formatLocalTime(time_t utcEpoch, int32_t utcOffsetSeconds,
                               int32_t daylightOffsetSeconds, char* buffer,
                               size_t bufferSize);
};

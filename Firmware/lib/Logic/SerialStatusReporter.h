#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "IClock.h"
#include "ISerialPort.h"
#include "ISerialStatusControl.h"
#include "MqttService.h"
#include "NtpService.h"
#include "WifiManager.h"

struct SerialStatusReporterConfig
{
  uint32_t intervalMs;
};

/**
 * Writes USB-gated WiFi, NTP, and MQTT status snapshots on a configured
 * interval for the current power-on session.
 */
class SerialStatusReporter : public ISerialStatusControl
{
public:
  /**
   * Creates a status reporter using injected serial, clock, and services.
   *
   * @param serial Byte-oriented serial port.
   * @param clock Monotonic clock.
   * @param wifiManager WiFi connection manager.
   * @param ntpService NTP synchronization service.
   * @param mqttService MQTT connection manager.
   */
  SerialStatusReporter(ISerialPort& serial, IClock& clock,
                       WifiManager& wifiManager, NtpService& ntpService,
                       MqttService& mqttService);

  /**
   * Starts status reporting with the supplied snapshot interval.
   *
   * @param config Snapshot interval configuration.
   * @return Nothing.
   */
  void begin(const SerialStatusReporterConfig& config);

  /**
   * Replaces the snapshot interval for the rest of the power-on session.
   *
   * @param config New snapshot interval configuration.
   * @return Nothing.
   */
  void reconfigure(const SerialStatusReporterConfig& config);

  /**
   * Writes WiFi, NTP, and MQTT status lines when started, periodic reporting
   * is enabled, the USB link is plugged in, and the snapshot interval has
   * elapsed.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Enables or disables periodic snapshots for this power-on session.
   *
   * Enabling restarts the snapshot interval from the current time.
   *
   * @param enabled True to print on the snapshot interval.
   * @return Nothing.
   */
  void setReportingEnabled(bool enabled) override;

  /**
   * Reports whether periodic snapshots are enabled.
   *
   * @return True when periodic snapshots are enabled.
   */
  bool reportingEnabled() const override;

  /**
   * Writes one WiFi, NTP, and MQTT snapshot immediately.
   *
   * The USB link must be plugged in. Periodic reporting may be disabled.
   * A successful print restarts the snapshot interval.
   *
   * @return Nothing.
   */
  void printStatus() override;

  /**
   * Returns the active snapshot configuration.
   *
   * @return Active configuration.
   */
  const SerialStatusReporterConfig& config() const;

private:
  ISerialPort& _serial;
  IClock& _clock;
  WifiManager& _wifiManager;
  NtpService& _ntpService;
  MqttService& _mqttService;
  SerialStatusReporterConfig _config{};
  bool _started = false;
  bool _reportingEnabled = true;
  uint32_t _lastReportAt = 0;

  /**
   * Writes the WiFi, NTP, and MQTT lines.
   *
   * @return Nothing.
   */
  void _writeSnapshot();

  /**
   * Writes the current WiFi manager state.
   *
   * A connected station with an address includes that address and RSSI.
   * A connected station without an address includes RSSI only.
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
   * Writes the current MQTT service state.
   *
   * @return Nothing.
   */
  void _writeMqttStatus();

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
   * Maps an MQTT service state to its serial label.
   *
   * @param state MQTT service state.
   * @return Status label.
   */
  static const char* _mqttStateName(MqttServiceState state);

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

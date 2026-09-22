#include "SerialStatusReporter.h"

#include <cstdio>
#include <cstdint>
#include <ctime>

namespace
{
constexpr int64_t secondsPerDay = 86400;

/**
 * Converts days since the Unix epoch to a Gregorian civil date.
 *
 * @param days Days since 1970-01-01.
 * @param year Output year.
 * @param month Output month, 1-12.
 * @param day Output day of month, 1-31.
 * @return Nothing.
 */
void civilFromDays(int64_t days, int32_t& year, unsigned& month, unsigned& day)
{
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int32_t>(yoe) + static_cast<int32_t>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year += (month <= 2);
}
}


// ========== Construction ==========

SerialStatusReporter::SerialStatusReporter(ISerialPort& serial, IClock& clock,
                                           WifiManager& wifiManager,
                                           NtpService& ntpService,
                                           MqttService& mqttService)
  : _serial(serial),
    _clock(clock),
    _wifiManager(wifiManager),
    _ntpService(ntpService),
    _mqttService(mqttService)
{
}


// ========== Public API ==========

/**
 * Starts status reporting with the supplied snapshot interval.
 *
 * @param config Snapshot interval configuration.
 * @return Nothing.
 */
void SerialStatusReporter::begin(const SerialStatusReporterConfig& config)
{
  _config = config;
  _started = true;
}

/**
 * Replaces the snapshot interval for the rest of the power-on session.
 *
 * @param config New snapshot interval configuration.
 * @return Nothing.
 */
void SerialStatusReporter::reconfigure(const SerialStatusReporterConfig& config)
{
  _config = config;
}

/**
 * Writes WiFi, NTP, and MQTT status lines when started, the USB link is
 * plugged in, and the snapshot interval has elapsed.
 *
 * @return Nothing.
 */
void SerialStatusReporter::update()
{
  if (!_started || !_serial.isPlugged())
  {
    return;
  }

  const uint32_t now = _clock.millis();
  if (!_hasElapsed(now, _lastReportAt, _config.intervalMs))
  {
    return;
  }

  _lastReportAt = now;
  _writeWifiStatus();
  _writeNtpStatus();
  _writeMqttStatus();
}

/**
 * Returns the active snapshot configuration.
 *
 * @return Active configuration.
 */
const SerialStatusReporterConfig& SerialStatusReporter::config() const
{
  return _config;
}


// ========== Private Helpers ==========

/**
 * Writes the current WiFi manager state, including RSSI when connected.
 *
 * @return Nothing.
 */
void SerialStatusReporter::_writeWifiStatus()
{
  char line[64];
  if (_wifiManager.state() == WifiManagerState::Connected)
  {
    std::snprintf(line, sizeof(line), "WiFi Status: Connected (%ld dBm)",
                  static_cast<long>(_wifiManager.rssi()));
  }
  else
  {
    std::snprintf(line, sizeof(line), "WiFi Status: %s",
                  _wifiStateName(_wifiManager.state()));
  }
  _serial.writeLine(line);
}

/**
 * Writes the current NTP state, including local time when synchronized.
 *
 * @return Nothing.
 */
void SerialStatusReporter::_writeNtpStatus()
{
  char line[64];
  if (_ntpService.state() == NtpServiceState::Synchronized)
  {
    char timestamp[20];
    _formatLocalTime(_ntpService.currentTime(),
                     _ntpService.config().utcOffsetSeconds,
                     _ntpService.config().daylightOffsetSeconds, timestamp,
                     sizeof(timestamp));
    std::snprintf(line, sizeof(line), "NTP Status: Synchronized (%s)",
                  timestamp);
  }
  else
  {
    std::snprintf(line, sizeof(line), "NTP Status: %s",
                  _ntpStateName(_ntpService.state()));
  }
  _serial.writeLine(line);
}

/**
 * Writes the current MQTT service state.
 *
 * @return Nothing.
 */
void SerialStatusReporter::_writeMqttStatus()
{
  char line[64];
  std::snprintf(line, sizeof(line), "MQTT Status: %s",
                _mqttStateName(_mqttService.state()));
  _serial.writeLine(line);
}

/**
 * Tests elapsed time using wrap-safe unsigned arithmetic.
 *
 * @param now Current monotonic time.
 * @param since Start time.
 * @param duration Required duration.
 * @return True when duration has elapsed.
 */
bool SerialStatusReporter::_hasElapsed(uint32_t now, uint32_t since,
                                       uint32_t duration)
{
  return static_cast<uint32_t>(now - since) >= duration;
}

/**
 * Maps a WiFi manager state to its serial label.
 *
 * @param state WiFi manager state.
 * @return Status label.
 */
const char* SerialStatusReporter::_wifiStateName(WifiManagerState state)
{
  switch (state)
  {
    case WifiManagerState::Connecting:
      return "Connecting";
    case WifiManagerState::Connected:
      return "Connected";
    case WifiManagerState::Backoff:
      return "Backoff";
    case WifiManagerState::Idle:
    default:
      return "Idle";
  }
}

/**
 * Maps an NTP service state to its serial label.
 *
 * @param state NTP service state.
 * @return Status label.
 */
const char* SerialStatusReporter::_ntpStateName(NtpServiceState state)
{
  switch (state)
  {
    case NtpServiceState::WaitingForSync:
      return "WaitingForSync";
    case NtpServiceState::Synchronized:
      return "Synchronized";
    case NtpServiceState::Idle:
    default:
      return "Idle";
  }
}

/**
 * Maps an MQTT service state to its serial label.
 *
 * @param state MQTT service state.
 * @return Status label.
 */
const char* SerialStatusReporter::_mqttStateName(MqttServiceState state)
{
  switch (state)
  {
    case MqttServiceState::Unconfigured:
      return "Unconfigured";
    case MqttServiceState::WaitingForNetwork:
      return "WaitingForNetwork";
    case MqttServiceState::Connecting:
      return "Connecting";
    case MqttServiceState::Connected:
      return "Connected";
    case MqttServiceState::Backoff:
      return "Backoff";
    case MqttServiceState::Idle:
    default:
      return "Idle";
  }
}

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
void SerialStatusReporter::_formatLocalTime(time_t utcEpoch,
                                            int32_t utcOffsetSeconds,
                                            int32_t daylightOffsetSeconds,
                                            char* buffer, size_t bufferSize)
{
  const int64_t localSeconds =
      static_cast<int64_t>(utcEpoch) + utcOffsetSeconds + daylightOffsetSeconds;
  int64_t days = localSeconds / secondsPerDay;
  int64_t secondsOfDay = localSeconds % secondsPerDay;
  if (secondsOfDay < 0)
  {
    secondsOfDay += secondsPerDay;
    days -= 1;
  }

  int32_t year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  civilFromDays(days, year, month, day);

  const unsigned hour = static_cast<unsigned>(secondsOfDay / 3600);
  const unsigned minute = static_cast<unsigned>((secondsOfDay % 3600) / 60);
  const unsigned second = static_cast<unsigned>(secondsOfDay % 60);

  std::snprintf(buffer, bufferSize, "%04d-%02u-%02u %02u:%02u:%02u", year,
                month, day, hour, minute, second);
}

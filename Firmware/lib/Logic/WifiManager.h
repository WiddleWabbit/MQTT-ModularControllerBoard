#pragma once

#include <cstdint>
#include <string>

#include "IClock.h"
#include "IWifi.h"

struct WifiManagerConfig
{
  const char* ssid;
  const char* password;
  uint32_t connectTimeoutMs;
  uint32_t initialRetryDelayMs;
  uint32_t maxRetryDelayMs;
  const char* hostname;
};

enum class WifiManagerState : uint8_t
{
  Idle,
  Connecting,
  Connected,
  Backoff
};

/**
 * Owns nonblocking WiFi connection state and retry timing.
 */
class WifiManager
{
public:
  /**
   * Creates a WiFi manager using injected platform services.
   *
   * @param wifi WiFi station interface.
   * @param clock Monotonic clock.
   * @param config Connection and retry configuration.
   */
  WifiManager(IWifi& wifi, IClock& clock, const WifiManagerConfig& config);

  /**
   * Starts the first nonblocking connection attempt.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Advances the connection state machine without blocking.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Replaces connection settings and restarts the state machine.
   *
   * @param config New connection and retry configuration.
   * @return Nothing.
   */
  void reconfigure(const WifiManagerConfig& config);

  /**
   * Returns the active WiFi configuration.
   *
   * @return Active configuration.
   */
  const WifiManagerConfig& config() const;

  /**
   * Reads the current manager state.
   *
   * @return Current state.
   */
  WifiManagerState state() const;

  /**
   * Reports whether WiFi is currently connected.
   *
   * @return True when the manager is connected.
   */
  bool isConnected() const;

  /**
   * Returns the delay used before the next retry.
   *
   * @return Current retry delay in milliseconds.
   */
  uint32_t currentRetryDelayMs() const;

  /**
   * Reads the current station signal strength.
   *
   * @return Received signal strength in dBm.
   */
  int32_t rssi() const;

  /**
   * Reads the current station address.
   *
   * @return Assigned IPv4 address, or 0.0.0.0 when none is assigned.
   */
  Ipv4Address localIp() const;

private:
  IWifi& _wifi;
  IClock& _clock;
  WifiManagerConfig _config;
  WifiManagerState _state = WifiManagerState::Idle;
  uint32_t _attemptStartedAt = 0;
  uint32_t _retryAvailableAt = 0;
  uint32_t _retryDelayMs = 0;
  std::string _appliedHostname;

  /**
   * Starts a connection attempt and records its start time.
   *
   * @return Nothing.
   */
  void _startConnectionAttempt();

  /**
   * Commits a changed non-empty hostname before the station starts.
   *
   * @return Nothing.
   */
  void _commitHostname();

  /**
   * Schedules the next attempt and advances exponential backoff.
   *
   * @return Nothing.
   */
  void _scheduleRetry();

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
   * Tests whether an absolute retry time has been reached.
   *
   * @param now Current monotonic time.
   * @param due Retry time.
   * @return True when retry time is due.
   */
  static bool _isDue(uint32_t now, uint32_t due);
};

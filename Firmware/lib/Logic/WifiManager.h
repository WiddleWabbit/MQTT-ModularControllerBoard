#pragma once

#include <cstdint>

#include "IClock.h"
#include "IWifi.h"

struct WifiManagerConfig
{
  const char* ssid;
  const char* password;
  uint32_t connectTimeoutMs;
  uint32_t initialRetryDelayMs;
  uint32_t maxRetryDelayMs;
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

private:
  IWifi& _wifi;
  IClock& _clock;
  WifiManagerConfig _config;
  WifiManagerState _state = WifiManagerState::Idle;
  uint32_t _attemptStartedAt = 0;
  uint32_t _retryAvailableAt = 0;
  uint32_t _retryDelayMs = 0;

  /**
   * Starts a connection attempt and records its start time.
   *
   * @return Nothing.
   */
  void _startConnectionAttempt();

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

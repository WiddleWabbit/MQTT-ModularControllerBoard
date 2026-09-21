#include "WifiManager.h"

// ========== Construction ==========

WifiManager::WifiManager(IWifi& wifi, IClock& clock,
                         const WifiManagerConfig& config)
  : _wifi(wifi),
    _clock(clock),
    _config(config),
    _retryDelayMs(config.initialRetryDelayMs)
{
}


// ========== Public API ==========

/**
 * Starts the first nonblocking connection attempt.
 *
 * @return Nothing.
 */
void WifiManager::begin()
{
  _retryDelayMs = _config.initialRetryDelayMs;
  _state = WifiManagerState::Connecting;
  _startConnectionAttempt();
}

/**
 * Advances the connection state machine without blocking.
 *
 * @return Nothing.
 */
void WifiManager::update()
{
  const uint32_t now = _clock.millis();

  if (_state == WifiManagerState::Idle)
  {
    return;
  }

  if (_state == WifiManagerState::Connecting)
  {
    if (_wifi.status() == WifiLinkState::Connected)
    {
      _state = WifiManagerState::Connected;
      _retryDelayMs = _config.initialRetryDelayMs;
      return;
    }

    if (_hasElapsed(now, _attemptStartedAt, _config.connectTimeoutMs))
    {
      _wifi.disconnect();
      _scheduleRetry();
    }
    return;
  }

  if (_state == WifiManagerState::Connected)
  {
    if (_wifi.status() != WifiLinkState::Connected)
    {
      _wifi.disconnect();
      _scheduleRetry();
    }
    return;
  }

  if (_isDue(now, _retryAvailableAt))
  {
    _startConnectionAttempt();
  }
}

void WifiManager::reconfigure(const WifiManagerConfig& config)
{
  _wifi.disconnect();
  _config = config;
  _state = WifiManagerState::Idle;
  _retryDelayMs = 0;
}

const WifiManagerConfig& WifiManager::config() const
{
  return _config;
}

/**
 * Reads the current manager state.
 *
 * @return Current state.
 */
WifiManagerState WifiManager::state() const
{
  return _state;
}

/**
 * Reports whether WiFi is currently connected.
 *
 * @return True when the manager is connected.
 */
bool WifiManager::isConnected() const
{
  return _state == WifiManagerState::Connected;
}

/**
 * Returns the delay used before the next retry.
 *
 * @return Current retry delay in milliseconds.
 */
uint32_t WifiManager::currentRetryDelayMs() const
{
  return _retryDelayMs;
}

/**
 * Reads the current station signal strength.
 *
 * @return Received signal strength in dBm.
 */
int32_t WifiManager::rssi() const
{
  return _wifi.rssi();
}

// ========== Private Helpers ==========

/**
 * Starts a connection attempt and records its start time.
 *
 * @return Nothing.
 */
void WifiManager::_startConnectionAttempt()
{
  _wifi.begin(_config.ssid, _config.password);
  _attemptStartedAt = _clock.millis();
  _state = WifiManagerState::Connecting;
}

/**
 * Schedules the next attempt and advances exponential backoff.
 *
 * @return Nothing.
 */
void WifiManager::_scheduleRetry()
{
  const uint32_t now = _clock.millis();
  _retryAvailableAt = now + _retryDelayMs;
  _state = WifiManagerState::Backoff;

  const uint32_t doubledDelay = _retryDelayMs * 2U;
  if (doubledDelay < _retryDelayMs ||
      doubledDelay > _config.maxRetryDelayMs)
  {
    _retryDelayMs = _config.maxRetryDelayMs;
  }
  else
  {
    _retryDelayMs = doubledDelay;
  }
}

/**
 * Tests elapsed time using wrap-safe unsigned arithmetic.
 *
 * @param now Current monotonic time.
 * @param since Start time.
 * @param duration Required duration.
 * @return True when duration has elapsed.
 */
bool WifiManager::_hasElapsed(uint32_t now, uint32_t since, uint32_t duration)
{
  return static_cast<uint32_t>(now - since) >= duration;
}

/**
 * Tests whether an absolute retry time has been reached.
 *
 * @param now Current monotonic time.
 * @param due Retry time.
 * @return True when retry time is due.
 */
bool WifiManager::_isDue(uint32_t now, uint32_t due)
{
  return static_cast<int32_t>(now - due) >= 0;
}

#include "WifiConnectionManager.h"

#include <cstring>

// ========== Construction ==========

WifiConnectionManager::WifiConnectionManager(
    IWifiStation& wifi, IClock& clock, ISystemControl& system,
    unsigned long connectionTimeoutMs, unsigned long maxTimeouts)
    : _wifi(wifi),
      _clock(clock),
      _system(system),
      _connectionTimeoutMs(connectionTimeoutMs),
      _maxTimeouts(maxTimeouts)
{
}

// ========== State Machine ==========

void WifiConnectionManager::start(const char* ssid, const char* password)
{
  if (ssid == nullptr || std::strlen(ssid) == 0) {
    _state = WifiConnectionState::Idle;
    return;
  }

  _ssid = ssid;
  _password = password == nullptr ? "" : password;
  _timeoutCount = 0;
  beginAttempt();
}

void WifiConnectionManager::update()
{
  if (_state == WifiConnectionState::Idle ||
      _state == WifiConnectionState::RestartRequested) {
    return;
  }

  if (_wifi.isConnected()) {
    _state = WifiConnectionState::Connected;
    _timeoutCount = 0;
    return;
  }

  if (_state == WifiConnectionState::Connected) {
    beginAttempt();
    return;
  }

  if (_clock.nowMillis() - _attemptStartedAt < _connectionTimeoutMs) {
    return;
  }

  ++_timeoutCount;
  if (_restartOnFailure && _maxTimeouts > 0 &&
      _timeoutCount >= _maxTimeouts) {
    _state = WifiConnectionState::RestartRequested;
    _system.restart();
    return;
  }

  if (!_restartOnFailure && _maxTimeouts > 0 &&
      _timeoutCount >= _maxTimeouts) {
    _timeoutCount = 0;
  }

  beginAttempt();
}

void WifiConnectionManager::setRestartOnFailure(bool enable)
{
  _restartOnFailure = enable;
}

void WifiConnectionManager::setMaxTimeouts(unsigned long maxTimeouts)
{
  _maxTimeouts = maxTimeouts;
}

WifiConnectionState WifiConnectionManager::state() const
{
  return _state;
}

bool WifiConnectionManager::isConnected() const
{
  return _state == WifiConnectionState::Connected && _wifi.isConnected();
}

bool WifiConnectionManager::restartRequested() const
{
  return _state == WifiConnectionState::RestartRequested;
}

unsigned long WifiConnectionManager::timeoutCount() const
{
  return _timeoutCount;
}

void WifiConnectionManager::beginAttempt()
{
  _wifi.disconnect();
  _wifi.begin(_ssid, _password);
  _attemptStartedAt = _clock.nowMillis();
  _state = WifiConnectionState::Connecting;
}

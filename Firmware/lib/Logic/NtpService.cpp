#include "NtpService.h"

// ========== Construction ==========

NtpService::NtpService(INtpAdapter& adapter, IClock& clock,
                       const NtpConfig& config)
  : _adapter(adapter),
    _clock(clock),
    _config(config)
{
}


// ========== Public API ==========

/**
 * Starts NTP configuration.
 *
 * @return Nothing.
 */
void NtpService::begin()
{
  _adapter.configure(_config.server1, _config.server2, _config.server3,
                     _config.utcOffsetSeconds,
                     _config.daylightOffsetSeconds);
  _lastConfigureAt = _clock.millis();
  _state = NtpServiceState::WaitingForSync;
}

/**
 * Checks synchronization and periodically reconfigures NTP.
 *
 * @return Nothing.
 */
void NtpService::update()
{
  if (_state == NtpServiceState::Idle)
  {
    return;
  }

  if (_adapter.isSynchronized())
  {
    _state = NtpServiceState::Synchronized;
    return;
  }

  _state = NtpServiceState::WaitingForSync;
  const uint32_t now = _clock.millis();
  if (_hasElapsed(now, _lastConfigureAt, _config.retryIntervalMs))
  {
    _adapter.configure(_config.server1, _config.server2, _config.server3,
                       _config.utcOffsetSeconds,
                       _config.daylightOffsetSeconds);
    _lastConfigureAt = now;
  }
}

/**
 * Reads the current synchronization state.
 *
 * @return Current state.
 */
NtpServiceState NtpService::state() const
{
  return _state;
}

/**
 * Reports whether valid time is available.
 *
 * @return True when synchronized.
 */
bool NtpService::isSynchronized() const
{
  return _state == NtpServiceState::Synchronized;
}

/**
 * Reads the current synchronized epoch.
 *
 * @return Current time, or zero before synchronization.
 */
time_t NtpService::currentTime() const
{
  return isSynchronized() ? _adapter.currentTime() : static_cast<time_t>(0);
}


// ========== Private Helpers ==========

/**
 * Tests elapsed time using wrap-safe unsigned arithmetic.
 *
 * @param now Current monotonic time.
 * @param since Start time.
 * @param duration Required duration.
 * @return True when duration has elapsed.
 */
bool NtpService::_hasElapsed(uint32_t now, uint32_t since, uint32_t duration)
{
  return static_cast<uint32_t>(now - since) >= duration;
}

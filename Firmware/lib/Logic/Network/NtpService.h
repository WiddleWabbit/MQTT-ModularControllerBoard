#pragma once

#include <cstdint>
#include <ctime>

#include "IClock.h"
#include "INtpAdapter.h"

struct NtpConfig
{
  const char* server1;
  const char* server2;
  const char* server3;
  int32_t utcOffsetSeconds;
  int32_t daylightOffsetSeconds;
  uint32_t retryIntervalMs;
};

enum class NtpServiceState : uint8_t
{
  Idle,
  WaitingForSync,
  Synchronized
};

/**
 * Coordinates nonblocking NTP configuration and synchronization checks.
 */
class NtpService
{
public:
  /**
   * Creates an NTP service using injected adapter and clock.
   *
   * @param adapter Platform NTP adapter.
   * @param clock Monotonic clock.
   * @param config NTP servers, offsets, and retry interval.
   */
  NtpService(INtpAdapter& adapter, IClock& clock, const NtpConfig& config);

  /**
   * Starts NTP configuration.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Checks synchronization and periodically reconfigures NTP.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Reads the current synchronization state.
   *
   * @return Current state.
   */
  NtpServiceState state() const;

  /**
   * Reports whether valid time is available.
   *
   * @return True when synchronized.
   */
  bool isSynchronized() const;

  /**
   * Reads the current synchronized epoch.
   *
   * @return Current time, or zero before synchronization.
   */
  time_t currentTime() const;

  /**
   * Returns the active NTP configuration.
   *
   * @return Active configuration.
   */
  const NtpConfig& config() const;

private:
  INtpAdapter& _adapter;
  IClock& _clock;
  NtpConfig _config;
  NtpServiceState _state = NtpServiceState::Idle;
  uint32_t _lastConfigureAt = 0;

  /**
   * Tests elapsed time using wrap-safe unsigned arithmetic.
   *
   * @param now Current monotonic time.
   * @param since Start time.
   * @param duration Required duration.
   * @return True when duration has elapsed.
   */
  static bool _hasElapsed(uint32_t now, uint32_t since, uint32_t duration);
};

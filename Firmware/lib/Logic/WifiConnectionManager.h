#pragma once

#include "IClock.h"
#include "ISystemControl.h"
#include "IWifiStation.h"

enum class WifiConnectionState {
  Idle,
  Connecting,
  Connected,
  RestartRequested
};

class WifiConnectionManager {
public:
  /**
   * Creates a non-blocking WiFi station connection state machine.
   *
   * @param wifi Station driver used to start and inspect connections.
   * @param clock Monotonic clock used for connection timeouts.
   * @param system System control used when restart-on-failure is enabled.
   * @param connectionTimeoutMs Maximum duration of one connection attempt.
   * @param maxTimeouts Failure limit; zero means unlimited retries.
   */
  WifiConnectionManager(IWifiStation& wifi, IClock& clock,
                        ISystemControl& system,
                        unsigned long connectionTimeoutMs = 30000,
                        unsigned long maxTimeouts = 100);

  /**
   * Starts a connection attempt when an SSID is supplied.
   *
   * @param ssid Network name to connect to.
   * @param password Network password, which may be empty.
   * @return Nothing.
   */
  void start(const char* ssid, const char* password);

  /**
   * Advances the connection state machine without blocking.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Enables or disables restart after the configured failure limit.
   *
   * @param enable True to request a system restart at the limit.
   * @return Nothing.
   */
  void setRestartOnFailure(bool enable);

  /**
   * Changes the failure limit.
   *
   * @param maxTimeouts Failure limit; zero means unlimited retries.
   * @return Nothing.
   */
  void setMaxTimeouts(unsigned long maxTimeouts);

  /**
   * Reports the current connection state.
   *
   * @return Current state.
   */
  WifiConnectionState state() const;

  /**
   * Reports whether the station is currently connected.
   *
   * @return True only when the state machine and driver both report connected.
   */
  bool isConnected() const;

  /**
   * Reports whether a restart has been requested.
   *
   * @return True after the failure policy requests a restart.
   */
  bool restartRequested() const;

  /**
   * Returns the number of timed-out attempts since the last connection.
   *
   * @return Number of timed-out attempts.
   */
  unsigned long timeoutCount() const;

private:
  /**
   * Starts one station attempt and records its start time.
   *
   * @return Nothing.
   */
  void beginAttempt();

  IWifiStation& _wifi;
  IClock& _clock;
  ISystemControl& _system;
  const unsigned long _connectionTimeoutMs;
  unsigned long _maxTimeouts;
  unsigned long _attemptStartedAt = 0;
  unsigned long _timeoutCount = 0;
  const char* _ssid = nullptr;
  const char* _password = nullptr;
  WifiConnectionState _state = WifiConnectionState::Idle;
  bool _restartOnFailure = false;
};

#include <unity.h>

#include "WifiConnectionManager.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeSystemControl.h"
#include "fakes/FakeWifiStation.h"

namespace {
constexpr unsigned long connectionTimeout = 1000;
constexpr unsigned long maxTimeouts = 2;
}

// Verifies that an unstarted manager remains inert.
void test_update_before_start_is_a_noop()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.update();

  TEST_ASSERT_EQUAL(WifiConnectionState::Idle, manager.state());
  TEST_ASSERT_FALSE(manager.isConnected());
  TEST_ASSERT_FALSE(manager.restartRequested());
  TEST_ASSERT_EQUAL_UINT(0, wifi.beginCallCount);
}

// Verifies that a valid configuration starts one station attempt.
void test_starts_station_connection_with_supplied_credentials()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("garden-network", "secret");

  TEST_ASSERT_EQUAL(WifiConnectionState::Connecting, manager.state());
  TEST_ASSERT_EQUAL_UINT(1, wifi.disconnectCallCount);
  TEST_ASSERT_EQUAL_UINT(1, wifi.beginCallCount);
  TEST_ASSERT_EQUAL_STRING("garden-network", wifi.lastSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret", wifi.lastPassword.c_str());
}

// Verifies that an empty SSID is rejected without driver interaction.
void test_rejects_empty_ssid_without_touching_wifi()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("", "secret");

  TEST_ASSERT_EQUAL(WifiConnectionState::Idle, manager.state());
  TEST_ASSERT_EQUAL_UINT(0, wifi.beginCallCount);
  TEST_ASSERT_FALSE(manager.isConnected());
}

// Verifies that a null password is normalized to an empty password.
void test_uses_empty_password_when_password_is_null()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", nullptr);

  TEST_ASSERT_EQUAL_STRING("", wifi.lastPassword.c_str());
}

// Verifies that a connection clears the timeout history.
void test_connected_update_transitions_state_and_clears_failures()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", "password");
  clock.advance(connectionTimeout);
  manager.update();
  TEST_ASSERT_EQUAL_UINT(1, manager.timeoutCount());

  wifi.connected = true;
  manager.update();

  TEST_ASSERT_EQUAL(WifiConnectionState::Connected, manager.state());
  TEST_ASSERT_TRUE(manager.isConnected());
  TEST_ASSERT_EQUAL_UINT(0, manager.timeoutCount());
}

// Verifies that an active attempt is not retried prematurely.
void test_does_not_retry_before_timeout()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", "password");
  clock.advance(connectionTimeout - 1);
  manager.update();

  TEST_ASSERT_EQUAL_UINT(1, wifi.beginCallCount);
  TEST_ASSERT_EQUAL_UINT(0, manager.timeoutCount());
}

// Verifies that a timed-out attempt is retried and counted.
void test_retries_after_timeout_and_records_attempt()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", "password");
  clock.advance(connectionTimeout);
  manager.update();

  TEST_ASSERT_EQUAL_UINT(2, wifi.beginCallCount);
  TEST_ASSERT_EQUAL_UINT(1, manager.timeoutCount());
  TEST_ASSERT_EQUAL(WifiConnectionState::Connecting, manager.state());
}

// Verifies that a lost connection starts a fresh attempt immediately.
void test_lost_connection_starts_new_attempt_immediately()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", "password");
  wifi.connected = true;
  manager.update();
  TEST_ASSERT_EQUAL(WifiConnectionState::Connected, manager.state());

  wifi.connected = false;
  manager.update();

  TEST_ASSERT_EQUAL_UINT(2, wifi.beginCallCount);
  TEST_ASSERT_EQUAL(WifiConnectionState::Connecting, manager.state());
}

// Verifies that the restart policy stops retries at its configured limit.
void test_requests_restart_at_configured_failure_limit()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);
  manager.setRestartOnFailure(true);

  manager.start("network", "password");
  clock.advance(connectionTimeout);
  manager.update();
  clock.advance(connectionTimeout);
  manager.update();

  TEST_ASSERT_EQUAL_UINT(1, system.restartCallCount);
  TEST_ASSERT_EQUAL(WifiConnectionState::RestartRequested, manager.state());
  TEST_ASSERT_TRUE(manager.restartRequested());

  clock.advance(connectionTimeout * 2);
  manager.update();
  TEST_ASSERT_EQUAL_UINT(1, system.restartCallCount);
  TEST_ASSERT_EQUAL_UINT(2, wifi.beginCallCount);
}

// Verifies that changing the failure limit affects the next timeout.
void test_updates_failure_limit_when_reconfigured()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);
  manager.setMaxTimeouts(1);
  manager.setRestartOnFailure(true);

  manager.start("network", "password");
  clock.advance(connectionTimeout);
  manager.update();

  TEST_ASSERT_EQUAL_UINT(1, system.restartCallCount);
  TEST_ASSERT_TRUE(manager.restartRequested());
}

// Verifies that non-restarting retries periodically reset their count.
void test_resets_failure_count_when_restart_is_disabled()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout,
                                maxTimeouts);

  manager.start("network", "password");
  clock.advance(connectionTimeout);
  manager.update();
  clock.advance(connectionTimeout);
  manager.update();

  TEST_ASSERT_EQUAL_UINT(0, system.restartCallCount);
  TEST_ASSERT_EQUAL_UINT(0, manager.timeoutCount());
  TEST_ASSERT_EQUAL_UINT(3, wifi.beginCallCount);
}

// Verifies that a zero limit permits unlimited retries.
void test_zero_failure_limit_allows_unlimited_retries()
{
  FakeWifiStation wifi;
  FakeClock clock;
  FakeSystemControl system;
  WifiConnectionManager manager(wifi, clock, system, connectionTimeout, 0);
  manager.setRestartOnFailure(true);

  manager.start("network", "password");
  for (unsigned int attempt = 0; attempt < 3; ++attempt) {
    clock.advance(connectionTimeout);
    manager.update();
  }

  TEST_ASSERT_EQUAL_UINT(0, system.restartCallCount);
  TEST_ASSERT_EQUAL_UINT(3, manager.timeoutCount());
  TEST_ASSERT_EQUAL_UINT(4, wifi.beginCallCount);
}

// Runs all desktop Unity test cases once.
void setup()
{
  UNITY_BEGIN();
  RUN_TEST(test_update_before_start_is_a_noop);
  RUN_TEST(test_starts_station_connection_with_supplied_credentials);
  RUN_TEST(test_rejects_empty_ssid_without_touching_wifi);
  RUN_TEST(test_uses_empty_password_when_password_is_null);
  RUN_TEST(test_connected_update_transitions_state_and_clears_failures);
  RUN_TEST(test_does_not_retry_before_timeout);
  RUN_TEST(test_retries_after_timeout_and_records_attempt);
  RUN_TEST(test_lost_connection_starts_new_attempt_immediately);
  RUN_TEST(test_requests_restart_at_configured_failure_limit);
  RUN_TEST(test_updates_failure_limit_when_reconfigured);
  RUN_TEST(test_resets_failure_count_when_restart_is_disabled);
  RUN_TEST(test_zero_failure_limit_allows_unlimited_retries);
  UNITY_END();
}

// Keeps the desktop test runner from performing repeated work.
void loop()
{
}

#include <unity.h>

#include "WifiConnectionManager.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeSystemControl.h"
#include "fakes/FakeWifiStation.h"

namespace {
constexpr unsigned long connectionTimeout = 1000;
constexpr unsigned long maxTimeouts = 2;
}

void test_begin_with_stored_credentials_skips_scan_and_enters_station_state();
void test_begin_without_credentials_scans_and_enters_portal_state();
void test_store_initialization_failure_falls_back_to_manual_portal();
void test_scan_failure_keeps_portal_available_for_manual_configuration();
void test_scan_filters_hidden_networks_and_keeps_strongest_duplicate();
void test_scan_limits_visible_networks_without_overflowing_controller_storage();
void test_network_at_out_of_range_returns_empty_network();
void test_manual_ssid_takes_precedence_over_selected_network();
void test_selected_ssid_is_used_when_manual_ssid_is_blank();
void test_selected_ssid_is_used_when_manual_ssid_is_whitespace();
void test_open_network_can_be_saved_without_a_password();
void test_blank_ssids_are_rejected_without_saving();
void test_whitespace_only_ssids_are_rejected_without_saving();
void test_null_inputs_are_rejected_without_saving();
void test_submission_before_begin_is_rejected();
void test_oversized_credentials_are_rejected_without_truncation();
void test_save_failure_keeps_portal_state_and_reports_failure();
void test_successful_submission_is_loaded_on_the_next_begin();
void test_clear_credentials_returns_controller_to_idle();
void test_portal_view_lists_networks_and_manual_entry();
void test_portal_view_escapes_ssid_markup();

void test_defaultsToPoolServerAndThirtyMinuteFrequency();
void test_requestsSynchronizationOnceWhenWifiFirstConnects();
void test_doesNotRepeatRequestBeforeFrequencyExpires();
void test_requestsAgainWhenFrequencyExpires();
void test_reconnectionTriggersAnImmediateSynchronization();
void test_updateIsNonblockingAndContinuesDrivingNtpClient();
void test_configurationCanBeChanged();
void test_emptyServerIsRejected();
void test_zeroFrequencyIsRejected();
void test_reportsSynchronizationState();

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


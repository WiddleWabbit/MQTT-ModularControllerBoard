#include <unity.h>

#include <cstring>
#include <string>

#include "WifiSetupController.h"
#include "WifiSetupPortalView.h"
#include "fakes/FakeWifiCredentialsStore.h"
#include "fakes/FakeWifiScanner.h"

namespace {
constexpr size_t maximumVisibleNetworks = 3;
}

void test_begin_with_stored_credentials_skips_scan_and_enters_station_state()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("other-network", -40, true);
  FakeWifiCredentialsStore store;
  store.setStoredCredentials("saved-network", "saved-password");
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  controller.begin();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::StationReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_EQUAL_UINT(0, scanner.scanCallCount);
  TEST_ASSERT_EQUAL_STRING("saved-network", controller.credentials().ssid);
  TEST_ASSERT_EQUAL_STRING("saved-password", controller.credentials().password);
}

void test_begin_without_credentials_scans_and_enters_portal_state()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("garden-network", -42, true);
  scanner.addNetwork("open-network", -67, false);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  controller.begin();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::PortalReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_EQUAL_UINT(1, scanner.scanCallCount);
  TEST_ASSERT_EQUAL_UINT(2, controller.networkCount());
  TEST_ASSERT_EQUAL_STRING("garden-network", controller.networkAt(0).ssid);
  TEST_ASSERT_TRUE(controller.networkAt(0).encrypted);
  TEST_ASSERT_FALSE(controller.networkAt(1).encrypted);
}

void test_store_initialization_failure_falls_back_to_manual_portal()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("ignored-network", -40, true);
  FakeWifiCredentialsStore store;
  store.beginResult = false;
  store.setStoredCredentials("saved-network", "password");
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  controller.begin();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::PortalReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_EQUAL_UINT(1, store.beginCallCount);
  TEST_ASSERT_EQUAL_UINT(1, scanner.scanCallCount);
  TEST_ASSERT_EQUAL_UINT(1, controller.networkCount());
}

void test_scan_failure_keeps_portal_available_for_manual_configuration()
{
  FakeWifiScanner scanner;
  scanner.scanResult = -1;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  controller.begin();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::PortalReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_TRUE(controller.scanFailed());
  TEST_ASSERT_EQUAL_UINT(0, controller.networkCount());
}

void test_scan_filters_hidden_networks_and_keeps_strongest_duplicate()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("", -20, true);
  scanner.addNetwork("network", -70, true);
  scanner.addNetwork("network", -35, false);
  scanner.addNetwork("another", -60, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, 10);

  controller.begin();

  TEST_ASSERT_EQUAL_UINT(2, controller.networkCount());
  TEST_ASSERT_EQUAL_STRING("network", controller.networkAt(0).ssid);
  TEST_ASSERT_EQUAL_INT(-35, controller.networkAt(0).rssi);
  TEST_ASSERT_FALSE(controller.networkAt(0).encrypted);
}

void test_scan_limits_visible_networks_without_overflowing_controller_storage()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("one", -10, true);
  scanner.addNetwork("two", -20, true);
  scanner.addNetwork("three", -30, true);
  scanner.addNetwork("four", -40, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  controller.begin();

  TEST_ASSERT_EQUAL_UINT(maximumVisibleNetworks, controller.networkCount());
  TEST_ASSERT_EQUAL_STRING("three", controller.networkAt(2).ssid);
}

void test_network_at_out_of_range_returns_empty_network()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  const WifiNetworkInfo& network = controller.networkAt(0);

  TEST_ASSERT_EQUAL_STRING("", network.ssid);
  TEST_ASSERT_EQUAL_INT(0, network.rssi);
  TEST_ASSERT_FALSE(network.encrypted);
}

void test_manual_ssid_takes_precedence_over_selected_network()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("selected-network", -40, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_TRUE(controller.submitCredentials(
      "selected-network", "manual-network", "manual-password"));
  TEST_ASSERT_EQUAL_STRING("manual-network",
                           store.lastSavedCredentials.ssid);
  TEST_ASSERT_EQUAL_STRING("manual-password",
                           store.lastSavedCredentials.password);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::StationReady),
                        static_cast<int>(controller.state()));
}

void test_selected_ssid_is_used_when_manual_ssid_is_blank()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("selected-network", -40, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_TRUE(
      controller.submitCredentials("selected-network", "", "password"));
  TEST_ASSERT_EQUAL_STRING("selected-network",
                           store.lastSavedCredentials.ssid);
}

void test_selected_ssid_is_used_when_manual_ssid_is_whitespace()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("selected-network", -40, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_TRUE(controller.submitCredentials(
      "selected-network", " \t", "password"));
  TEST_ASSERT_EQUAL_STRING("selected-network",
                           store.lastSavedCredentials.ssid);
}

void test_open_network_can_be_saved_without_a_password()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("open-network", -40, false);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_TRUE(
      controller.submitCredentials("open-network", "", ""));
  TEST_ASSERT_EQUAL_STRING("", store.lastSavedCredentials.password);
}

void test_blank_ssids_are_rejected_without_saving()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_FALSE(controller.submitCredentials("", "", "password"));
  TEST_ASSERT_EQUAL_UINT(0, store.saveCallCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::PortalReady),
                        static_cast<int>(controller.state()));
}

void test_null_inputs_are_rejected_without_saving()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_FALSE(controller.submitCredentials(nullptr, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT(0, store.saveCallCount);
}

void test_whitespace_only_ssids_are_rejected_without_saving()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_FALSE(
      controller.submitCredentials("   \t", "", "password"));
  TEST_ASSERT_EQUAL_UINT(0, store.saveCallCount);
}

void test_submission_before_begin_is_rejected()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);

  TEST_ASSERT_FALSE(
      controller.submitCredentials("network", "", "password"));
  TEST_ASSERT_EQUAL_UINT(0, store.saveCallCount);
}

void test_oversized_credentials_are_rejected_without_truncation()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();
  const std::string oversizedSsid(WifiCredentials::maximumSsidLength + 1, 's');

  TEST_ASSERT_FALSE(
      controller.submitCredentials(oversizedSsid.c_str(), "", "password"));
  TEST_ASSERT_EQUAL_UINT(0, store.saveCallCount);
}

void test_save_failure_keeps_portal_state_and_reports_failure()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  store.saveResult = false;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  TEST_ASSERT_FALSE(
      controller.submitCredentials("network", "", "password"));
  TEST_ASSERT_EQUAL_UINT(1, store.saveCallCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::PortalReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_TRUE(controller.saveFailed());
}

void test_successful_submission_is_loaded_on_the_next_begin()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();
  TEST_ASSERT_TRUE(
      controller.submitCredentials("network", "", "password"));

  controller.begin();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::StationReady),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_EQUAL_UINT(1, scanner.scanCallCount);
}

void test_clear_credentials_returns_controller_to_idle()
{
  FakeWifiScanner scanner;
  FakeWifiCredentialsStore store;
  store.setStoredCredentials("network", "password");
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();

  controller.clearCredentials();

  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiSetupState::Idle),
                        static_cast<int>(controller.state()));
  TEST_ASSERT_EQUAL_UINT(1, store.clearCallCount);
  TEST_ASSERT_EQUAL_STRING("", controller.credentials().ssid);
}

void test_portal_view_lists_networks_and_manual_entry()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("garden-network", -42, true);
  scanner.addNetwork("open-network", -67, false);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();
  WifiSetupPortalView view;

  const std::string html = view.render(controller);

  TEST_ASSERT_NOT_NULL(std::strstr(html.c_str(), "garden-network"));
  TEST_ASSERT_NOT_NULL(std::strstr(html.c_str(), "open-network"));
  TEST_ASSERT_NOT_NULL(std::strstr(html.c_str(), "name=\"manualSsid\""));
  TEST_ASSERT_NOT_NULL(std::strstr(html.c_str(), "name=\"ssid\""));
}

void test_portal_view_escapes_ssid_markup()
{
  FakeWifiScanner scanner;
  scanner.addNetwork("<unsafe>&", -42, true);
  FakeWifiCredentialsStore store;
  WifiSetupController controller(scanner, store, maximumVisibleNetworks);
  controller.begin();
  WifiSetupPortalView view;

  const std::string html = view.render(controller);

  TEST_ASSERT_NOT_NULL(std::strstr(html.c_str(), "&lt;unsafe&gt;&amp;"));
  TEST_ASSERT_NULL(std::strstr(html.c_str(), "<unsafe>&"));
}

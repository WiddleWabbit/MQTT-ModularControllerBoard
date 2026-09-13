#include <unity.h>

extern void test_update_before_start_is_a_noop();
extern void test_starts_station_connection_with_supplied_credentials();
extern void test_rejects_empty_ssid_without_touching_wifi();
extern void test_uses_empty_password_when_password_is_null();
extern void test_connected_update_transitions_state_and_clears_failures();
extern void test_does_not_retry_before_timeout();
extern void test_retries_after_timeout_and_records_attempt();
extern void test_lost_connection_starts_new_attempt_immediately();
extern void test_requests_restart_at_configured_failure_limit();
extern void test_updates_failure_limit_when_reconfigured();
extern void test_resets_failure_count_when_restart_is_disabled();
extern void test_zero_failure_limit_allows_unlimited_retries();
extern void test_begin_with_stored_credentials_skips_scan_and_enters_station_state();
extern void test_begin_without_credentials_scans_and_enters_portal_state();
extern void test_store_initialization_failure_falls_back_to_manual_portal();
extern void test_scan_failure_keeps_portal_available_for_manual_configuration();
extern void test_scan_filters_hidden_networks_and_keeps_strongest_duplicate();
extern void test_scan_limits_visible_networks_without_overflowing_controller_storage();
extern void test_network_at_out_of_range_returns_empty_network();
extern void test_manual_ssid_takes_precedence_over_selected_network();
extern void test_selected_ssid_is_used_when_manual_ssid_is_blank();
extern void test_selected_ssid_is_used_when_manual_ssid_is_whitespace();
extern void test_open_network_can_be_saved_without_a_password();
extern void test_blank_ssids_are_rejected_without_saving();
extern void test_whitespace_only_ssids_are_rejected_without_saving();
extern void test_null_inputs_are_rejected_without_saving();
extern void test_submission_before_begin_is_rejected();
extern void test_oversized_credentials_are_rejected_without_truncation();
extern void test_save_failure_keeps_portal_state_and_reports_failure();
extern void test_successful_submission_is_loaded_on_the_next_begin();
extern void test_clear_credentials_returns_controller_to_idle();
extern void test_portal_view_lists_networks_and_manual_entry();
extern void test_portal_view_escapes_ssid_markup();

extern void test_defaultsToPoolServerAndThirtyMinuteFrequency();
extern void test_requestsSynchronizationOnceWhenWifiFirstConnects();
extern void test_doesNotRepeatRequestBeforeFrequencyExpires();
extern void test_requestsAgainWhenFrequencyExpires();
extern void test_reconnectionTriggersAnImmediateSynchronization();
extern void test_updateIsNonblockingAndContinuesDrivingNtpClient();
extern void test_configurationCanBeChanged();
extern void test_emptyServerIsRejected();
extern void test_zeroFrequencyIsRejected();
extern void test_reportsSynchronizationState();
extern void test_returnsCurrentTimeWhenSynchronized();
extern void test_rejectsCurrentTimeWhenNotSynchronized();
extern void test_reportsSynchronizationOnlyWhenWifiAndNtpAreReady();
extern void test_setsTimezoneAndReturnsLocalTime();
extern void test_rejectsLocalTimeWhenNotSynchronizedOrTimezoneInvalid();

extern void test_mqtt_defaultsToDisconnectedWaitingForWifi();
extern void test_mqtt_connectsWhenWifiIsAvailable();
extern void test_mqtt_subscribesConfiguredTopicsAfterConnecting();
extern void test_mqtt_retriesAfterFailedConnectionAtConfiguredInterval();
extern void test_mqtt_reconnectsImmediatelyAfterDisconnect();
extern void test_mqtt_disconnectsWhenWifiIsLost();
extern void test_mqtt_loopRunsOnlyWhileConnected();
extern void test_mqtt_publishRequiresConnection();
extern void test_mqtt_publishFailureIsReturned();
extern void test_mqtt_routesIncomingMessages();
extern void test_mqtt_preservesBinaryPayloadLength();
extern void test_mqtt_rejectsInvalidConfigurationAndTopics();
extern void test_mqtt_rejectsSubscriptionFailureAndRetries();
extern void test_mqtt_subscribesTopicAddedWhileConnected();
extern void test_mqtt_fakeSupportsSequencedConnectionResults();

extern void test_serial_begin_records_baud_rate();
extern void test_serial_disconnected_does_not_block_or_record_output();
extern void test_serial_reconnects_after_starting_disconnected();
extern void test_serial_disconnect_and_reconnect_preserve_state_safely();
extern void test_serial_print_and_println_preserve_output_order();
extern void test_serial_printf_preserves_formatting_and_order();
extern void test_serial_print_numeric_values();
extern void test_serial_println_numeric_values_add_one_line_ending();
extern void test_serial_string_edge_cases_are_safe();
extern void test_serial_printf_null_format_is_ignored();
extern void test_serial_printf_empty_format_is_ordered();

void setUp()
{
}

void tearDown()
{
}

int main()
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
  RUN_TEST(test_begin_with_stored_credentials_skips_scan_and_enters_station_state);
  RUN_TEST(test_begin_without_credentials_scans_and_enters_portal_state);
  RUN_TEST(test_store_initialization_failure_falls_back_to_manual_portal);
  RUN_TEST(test_scan_failure_keeps_portal_available_for_manual_configuration);
  RUN_TEST(test_scan_filters_hidden_networks_and_keeps_strongest_duplicate);
  RUN_TEST(test_scan_limits_visible_networks_without_overflowing_controller_storage);
  RUN_TEST(test_network_at_out_of_range_returns_empty_network);
  RUN_TEST(test_manual_ssid_takes_precedence_over_selected_network);
  RUN_TEST(test_selected_ssid_is_used_when_manual_ssid_is_blank);
  RUN_TEST(test_selected_ssid_is_used_when_manual_ssid_is_whitespace);
  RUN_TEST(test_open_network_can_be_saved_without_a_password);
  RUN_TEST(test_blank_ssids_are_rejected_without_saving);
  RUN_TEST(test_whitespace_only_ssids_are_rejected_without_saving);
  RUN_TEST(test_null_inputs_are_rejected_without_saving);
  RUN_TEST(test_submission_before_begin_is_rejected);
  RUN_TEST(test_oversized_credentials_are_rejected_without_truncation);
  RUN_TEST(test_save_failure_keeps_portal_state_and_reports_failure);
  RUN_TEST(test_successful_submission_is_loaded_on_the_next_begin);
  RUN_TEST(test_clear_credentials_returns_controller_to_idle);
  RUN_TEST(test_portal_view_lists_networks_and_manual_entry);
  RUN_TEST(test_portal_view_escapes_ssid_markup);

  RUN_TEST(test_defaultsToPoolServerAndThirtyMinuteFrequency);
  RUN_TEST(test_requestsSynchronizationOnceWhenWifiFirstConnects);
  RUN_TEST(test_doesNotRepeatRequestBeforeFrequencyExpires);
  RUN_TEST(test_requestsAgainWhenFrequencyExpires);
  RUN_TEST(test_reconnectionTriggersAnImmediateSynchronization);
  RUN_TEST(test_updateIsNonblockingAndContinuesDrivingNtpClient);
  RUN_TEST(test_configurationCanBeChanged);
  RUN_TEST(test_emptyServerIsRejected);
  RUN_TEST(test_zeroFrequencyIsRejected);
  RUN_TEST(test_reportsSynchronizationState);
  RUN_TEST(test_returnsCurrentTimeWhenSynchronized);
  RUN_TEST(test_rejectsCurrentTimeWhenNotSynchronized);
  RUN_TEST(test_reportsSynchronizationOnlyWhenWifiAndNtpAreReady);
  RUN_TEST(test_setsTimezoneAndReturnsLocalTime);
  RUN_TEST(test_rejectsLocalTimeWhenNotSynchronizedOrTimezoneInvalid);

  RUN_TEST(test_mqtt_defaultsToDisconnectedWaitingForWifi);
  RUN_TEST(test_mqtt_connectsWhenWifiIsAvailable);
  RUN_TEST(test_mqtt_subscribesConfiguredTopicsAfterConnecting);
  RUN_TEST(test_mqtt_retriesAfterFailedConnectionAtConfiguredInterval);
  RUN_TEST(test_mqtt_reconnectsImmediatelyAfterDisconnect);
  RUN_TEST(test_mqtt_disconnectsWhenWifiIsLost);
  RUN_TEST(test_mqtt_loopRunsOnlyWhileConnected);
  RUN_TEST(test_mqtt_publishRequiresConnection);
  RUN_TEST(test_mqtt_publishFailureIsReturned);
  RUN_TEST(test_mqtt_routesIncomingMessages);
  RUN_TEST(test_mqtt_preservesBinaryPayloadLength);
  RUN_TEST(test_mqtt_rejectsInvalidConfigurationAndTopics);
  RUN_TEST(test_mqtt_rejectsSubscriptionFailureAndRetries);
  RUN_TEST(test_mqtt_subscribesTopicAddedWhileConnected);
  RUN_TEST(test_mqtt_fakeSupportsSequencedConnectionResults);

  RUN_TEST(test_serial_begin_records_baud_rate);
  RUN_TEST(test_serial_disconnected_does_not_block_or_record_output);
  RUN_TEST(test_serial_reconnects_after_starting_disconnected);
  RUN_TEST(test_serial_disconnect_and_reconnect_preserve_state_safely);
  RUN_TEST(test_serial_print_and_println_preserve_output_order);
  RUN_TEST(test_serial_printf_preserves_formatting_and_order);
  RUN_TEST(test_serial_print_numeric_values);
  RUN_TEST(test_serial_println_numeric_values_add_one_line_ending);
  RUN_TEST(test_serial_string_edge_cases_are_safe);
  RUN_TEST(test_serial_printf_null_format_is_ignored);
  RUN_TEST(test_serial_printf_empty_format_is_ordered);

  return UNITY_END();
}

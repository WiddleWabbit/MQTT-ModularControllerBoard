#include <string>

#include <unity.h>

#include "NetworkConfigRecord.h"
#include "NetworkRuntime.h"
#include "NtpService.h"
#include "SerialConfigController.h"
#include "SerialStatusReporter.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNetworkConfigStore.h"
#include "fakes/FakeNtpAdapter.h"
#include "fakes/FakePreferenceStore.h"
#include "fakes/FakeSerialPort.h"
#include "fakes/FakeSerialStatusControl.h"
#include "fakes/FakeWifi.h"

namespace
{
NetworkConfig config()
{
  return {"old", "oldpw", "old-broker", 1883, "controller", nullptr, nullptr};
}

WifiManagerConfig wifiConfig()
{
  return {"old", "oldpw", 1000, 100, 400};
}

MqttConfig mqttConfig()
{
  return {"controller", nullptr, nullptr, nullptr, 0, 100, 400};
}

struct CommandStack
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeWifi wifi;
  FakeNtpAdapter ntpAdapter;
  FakeMqttClient mqttClient;
  FakeSerialPort serial;
  WifiManager wifiManager;
  NtpService ntpService;
  MqttService mqttService;
  NetworkRuntime runtime;
  SerialStatusReporter reporter;
  SerialConfigController controller;

  CommandStack()
    : modules(clock),
      wifiManager(wifi, clock, wifiConfig()),
      ntpService(ntpAdapter, clock,
                 {"pool.ntp.org", "time.nist.gov", nullptr, 0, 0, 60000}),
      mqttService(mqttClient, clock, mqttConfig()),
      runtime(store, wifiManager, mqttService),
      reporter(serial, clock, wifiManager, ntpService, mqttService,
               modules.host),
      controller(serial, runtime, reporter)
  {
    const NetworkConfig defaults = {
      "old", "oldpw", "old-broker", 1883, "controller", nullptr, nullptr,
      "watering-controller", true};
    runtime.begin(defaults);
    reporter.begin({1000});
    reporter.setReportingEnabled(runtime.config().statusReporting);
  }
};
}

void testRuntimeLoadsPersistedConfiguration()
{
  FakeNetworkConfigStore store;
  store.seed({"saved", "pw", "broker", 1884, "id", "u", "p"});
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);

  TEST_ASSERT_TRUE(runtime.begin(config()));
  TEST_ASSERT_EQUAL_STRING("saved", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("saved", wifiManager.config().ssid);
}

void testSerialStagesUntilApplyAndGatesOnPlugState()
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  runtime.begin(config());
  FakeSerialPort serial;
  FakeSerialStatusControl statusControl;
  SerialConfigController controller(serial, runtime, statusControl);

  serial.feed("set wifi.ssid new-network\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("old", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(0, store.saveCallCount);

  serial.feed("apply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("new-network", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(1, store.saveCallCount);

  serial.plugged = false;
  serial.feed("set mqtt.host hidden\napply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("new-network", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(1, store.saveCallCount);

  serial.plugged = true;
  controller.update();
  TEST_ASSERT_EQUAL_STRING("hidden", runtime.config().mqttHost);
  TEST_ASSERT_EQUAL(2, store.saveCallCount);
}

void testRuntimeApplyFailureDoesNotChangeActiveConfiguration()
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  runtime.begin(config());
  store.saveResult = false;

  NetworkConfig replacement = {"new", "pw", "host", 1883, "id", nullptr, nullptr,
                              "plant-room", true};
  TEST_ASSERT_FALSE(runtime.apply(replacement, NetworkConfigFieldMask::all()));
  TEST_ASSERT_EQUAL_STRING("old", runtime.config().wifiSsid);
}

void testAppliedConfigurationIsOwnedFromLaterStagedEdits()
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  runtime.begin(config());
  FakeSerialPort serial;
  FakeSerialStatusControl statusControl;
  SerialConfigController controller(serial, runtime, statusControl);

  serial.feed("set wifi.ssid applied-network\napply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("applied-network", runtime.config().wifiSsid);

  serial.feed("set wifi.ssid staged-only\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("applied-network", runtime.config().wifiSsid);
}

void testApplyWithNoChangesDoesNotSave()
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  runtime.begin(config());
  FakeSerialPort serial;
  FakeSerialStatusControl statusControl;
  SerialConfigController controller(serial, runtime, statusControl);

  serial.feed("apply\n");
  controller.update();

  TEST_ASSERT_EQUAL(0, store.saveCallCount);
  TEST_ASSERT_EQUAL_STRING("OK applied", serial.output.back().c_str());
  TEST_ASSERT_EQUAL_STRING("old", runtime.config().wifiSsid);
}

void testApplyUpdatesOnlyPasswordAndKeepsStoredSsid()
{
  FakeNetworkConfigStore store;
  store.seed({"garden", "old-password", "broker", 1883, "controller", nullptr,
              nullptr});
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  FakeSerialPort serial;
  FakeSerialStatusControl statusControl;
  SerialConfigController controller(serial, runtime, statusControl);
  runtime.begin(config());
  const int disconnectsAfterBegin = client.disconnectCallCount;
  const int wifiBeginsAfterBegin = wifi.beginCallCount;

  serial.feed("set wifi.password new-password\napply\napply\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("garden", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("new-password", runtime.config().wifiPassword);
  TEST_ASSERT_EQUAL_STRING("broker", runtime.config().mqttHost);
  TEST_ASSERT_EQUAL_STRING("garden", store.wifiSsid().c_str());
  TEST_ASSERT_EQUAL_STRING("new-password", store.wifiPassword().c_str());
  TEST_ASSERT_EQUAL_STRING("broker", store.mqttHost().c_str());
  TEST_ASSERT_TRUE(store.lastFields.wifiPassword);
  TEST_ASSERT_FALSE(store.lastFields.wifiSsid);
  TEST_ASSERT_FALSE(store.lastFields.mqttHost);
  TEST_ASSERT_EQUAL(1, store.saveCallCount);
  TEST_ASSERT_EQUAL_STRING("OK staged", serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("OK applied", serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("garden", wifi.lastSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("new-password", wifi.lastPassword.c_str());
  TEST_ASSERT_EQUAL(wifiBeginsAfterBegin + 1, wifi.beginCallCount);
  TEST_ASSERT_EQUAL(disconnectsAfterBegin, client.disconnectCallCount);

  serial.feed("set mqtt.host other-broker\napply\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("garden", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("new-password", runtime.config().wifiPassword);
  TEST_ASSERT_EQUAL_STRING("other-broker", runtime.config().mqttHost);
  TEST_ASSERT_EQUAL_STRING("garden", store.wifiSsid().c_str());
  TEST_ASSERT_TRUE(store.lastFields.mqttHost);
  TEST_ASSERT_FALSE(store.lastFields.wifiPassword);
  TEST_ASSERT_EQUAL(wifiBeginsAfterBegin + 1, wifi.beginCallCount);
  TEST_ASSERT_EQUAL(disconnectsAfterBegin + 1, client.disconnectCallCount);
}

void testApplyRetriesDirtyFieldsAfterSaveFailure()
{
  FakeNetworkConfigStore store;
  store.seed({"garden", "old-password", "broker", 1883, "controller", nullptr,
              nullptr});
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  FakeSerialPort serial;
  FakeSerialStatusControl statusControl;
  SerialConfigController controller(serial, runtime, statusControl);
  runtime.begin(config());
  store.saveResult = false;

  serial.feed("set wifi.password new-password\napply\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("ERR apply", serial.output.back().c_str());
  TEST_ASSERT_EQUAL_STRING("old-password", runtime.config().wifiPassword);
  TEST_ASSERT_EQUAL_STRING("garden", runtime.config().wifiSsid);

  store.saveResult = true;
  serial.feed("apply\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("OK applied", serial.output.back().c_str());
  TEST_ASSERT_EQUAL_STRING("new-password", runtime.config().wifiPassword);
  TEST_ASSERT_EQUAL_STRING("garden", store.wifiSsid().c_str());
  TEST_ASSERT_TRUE(store.lastFields.wifiPassword);
  TEST_ASSERT_FALSE(store.lastFields.wifiSsid);
}

void testRecordSaveWritesOnlySelectedFields()
{
  FakePreferenceStore store;
  store.strings[NetworkConfigKeys::wifiSsid] = "garden";
  store.strings[NetworkConfigKeys::wifiPassword] = "old-password";
  store.strings[NetworkConfigKeys::mqttHost] = "broker";
  NetworkConfig update = {"ignored", "new-password", "ignored", 1883,
                          "ignored", nullptr, nullptr};
  NetworkConfigFieldMask fields;
  fields.wifiPassword = true;

  TEST_ASSERT_TRUE(NetworkConfigRecord::save(store, update, fields));
  TEST_ASSERT_EQUAL_STRING("garden",
                           store.strings[NetworkConfigKeys::wifiSsid].c_str());
  TEST_ASSERT_EQUAL_STRING(
    "new-password", store.strings[NetworkConfigKeys::wifiPassword].c_str());
  TEST_ASSERT_EQUAL_STRING("broker",
                           store.strings[NetworkConfigKeys::mqttHost].c_str());
  TEST_ASSERT_EQUAL(0, store.writeCounts[NetworkConfigKeys::wifiSsid]);
  TEST_ASSERT_EQUAL(1, store.writeCounts[NetworkConfigKeys::wifiPassword]);
}

void testRecordEmptyStringSaveSucceedsWhenKeyIsStored()
{
  FakePreferenceStore store;
  NetworkConfig update = {"", "", "", 1883, "", nullptr, nullptr};
  NetworkConfigFieldMask fields;
  fields.mqttHost = true;

  TEST_ASSERT_TRUE(NetworkConfigRecord::save(store, update, fields));
  TEST_ASSERT_TRUE(store.contains(NetworkConfigKeys::mqttHost));
  TEST_ASSERT_EQUAL_STRING("", store.strings[NetworkConfigKeys::mqttHost].c_str());
}

void testRecordEmptyStringSaveFailsWhenKeyIsMissing()
{
  FakePreferenceStore store;
  store.rejectEmpty = true;
  NetworkConfig update = {"", "", "", 1883, "", nullptr, nullptr};
  NetworkConfigFieldMask fields;
  fields.mqttHost = true;

  TEST_ASSERT_FALSE(NetworkConfigRecord::save(store, update, fields));
  TEST_ASSERT_FALSE(store.contains(NetworkConfigKeys::mqttHost));
}

void testRecordNonEmptyWriteFailureReturnsFalse()
{
  FakePreferenceStore store;
  store.failKeys.insert(NetworkConfigKeys::wifiSsid);
  NetworkConfig update = {"garden", "secret", "", 1883, "", nullptr, nullptr};
  NetworkConfigFieldMask fields;
  fields.wifiSsid = true;
  fields.wifiPassword = true;

  TEST_ASSERT_FALSE(NetworkConfigRecord::save(store, update, fields));
  TEST_ASSERT_FALSE(store.contains(NetworkConfigKeys::wifiSsid));
  TEST_ASSERT_EQUAL_STRING(
    "secret", store.strings[NetworkConfigKeys::wifiPassword].c_str());
}

void testRecordLoadUsesDefaultClientIdWithoutReadingMissingKey()
{
  FakePreferenceStore store;
  store.strings[NetworkConfigKeys::wifiSsid] = "garden";
  store.strings[NetworkConfigKeys::mqttHost] = "";
  NetworkConfig defaults = {"", "", "", 1883, "watering-controller", nullptr,
                            nullptr};
  NetworkConfigData data;

  TEST_ASSERT_TRUE(NetworkConfigRecord::load(store, defaults, data));
  TEST_ASSERT_EQUAL_STRING("garden", data.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("", data.mqttHost.c_str());
  TEST_ASSERT_EQUAL_STRING("watering-controller", data.mqttClientId.c_str());
  TEST_ASSERT_EQUAL_STRING(
    "Config: mqtt client id is not stored; using watering-controller",
    data.warning.c_str());
  TEST_ASSERT_EQUAL(0, store.readCounts[NetworkConfigKeys::mqttClientId]);
  TEST_ASSERT_EQUAL_STRING(
    "watering-controller",
    store.strings[NetworkConfigKeys::mqttClientId].c_str());
}

void testRecordLoadFailureDoesNotWarnWhenNothingIsStored()
{
  FakePreferenceStore store;
  NetworkConfig defaults = {"", "", "", 1883, "watering-controller", nullptr,
                            nullptr};
  NetworkConfigData data;
  data.warning = "stale";

  TEST_ASSERT_FALSE(NetworkConfigRecord::load(store, defaults, data));
  TEST_ASSERT_EQUAL(0, store.readCounts[NetworkConfigKeys::mqttClientId]);
  TEST_ASSERT_EQUAL(0, store.writeCounts[NetworkConfigKeys::mqttClientId]);
}

void testRecordFailedClientRepairStillLoads()
{
  FakePreferenceStore store;
  store.strings[NetworkConfigKeys::wifiSsid] = "garden";
  store.failKeys.insert(NetworkConfigKeys::mqttClientId);
  NetworkConfig defaults = {"", "", "", 1883, "watering-controller", nullptr,
                            nullptr};
  NetworkConfigData data;

  TEST_ASSERT_TRUE(NetworkConfigRecord::load(store, defaults, data));
  TEST_ASSERT_EQUAL_STRING("watering-controller", data.mqttClientId.c_str());
  TEST_ASSERT_FALSE(data.warning.empty());
  TEST_ASSERT_FALSE(store.contains(NetworkConfigKeys::mqttClientId));
}

void testSetStatusOffRemainsEnabledUntilApply()
{
  CommandStack stack;
  stack.serial.feed("set status off\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("OK staged", stack.serial.output.back().c_str());
  TEST_ASSERT_TRUE(stack.reporter.reportingEnabled());
  TEST_ASSERT_EQUAL(0, stack.store.saveCallCount);

  const size_t before = stack.serial.output.size();
  stack.clock.advance(1000);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(before + 7, stack.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting",
                           stack.serial.output[before].c_str());
}

void testApplyStatusOffStoresZeroAndStopsPrints()
{
  CommandStack stack;
  stack.serial.feed("set status off\napply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("OK staged", stack.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output[1].c_str());
  TEST_ASSERT_FALSE(stack.reporter.reportingEnabled());
  TEST_ASSERT_FALSE(stack.runtime.config().statusReporting);
  TEST_ASSERT_FALSE(stack.store.statusReporting());
  TEST_ASSERT_TRUE(stack.store.lastFields.statusReporting);
  TEST_ASSERT_FALSE(stack.store.lastFields.wifiHostname);

  const size_t afterApply = stack.serial.output.size();
  stack.clock.advance(1000);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(afterApply, stack.serial.output.size());
}

void testApplyStatusOnResumesAfterInterval()
{
  CommandStack stack;
  stack.serial.feed("set status off\napply\nset status on\napply\n");
  stack.controller.update();

  TEST_ASSERT_TRUE(stack.reporter.reportingEnabled());
  TEST_ASSERT_TRUE(stack.runtime.config().statusReporting);
  TEST_ASSERT_TRUE(stack.store.statusReporting());

  const size_t afterApply = stack.serial.output.size();
  stack.clock.advance(999);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(afterApply, stack.serial.output.size());

  stack.clock.advance(1);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(afterApply + 7, stack.serial.output.size());
}

void testStatusCommandPrintsWhileReportingIsOff()
{
  CommandStack stack;
  stack.serial.feed("set status off\nstatus\n");
  stack.controller.update();
  TEST_ASSERT_TRUE(stack.reporter.reportingEnabled());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting",
                           stack.serial.output[1].c_str());

  stack.serial.feed("apply\nstatus\n");
  stack.controller.update();
  TEST_ASSERT_FALSE(stack.reporter.reportingEnabled());
  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output[8].c_str());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting",
                           stack.serial.output[9].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Idle", stack.serial.output[10].c_str());
  TEST_ASSERT_EQUAL_STRING("MQTT Status: WaitingForNetwork",
                           stack.serial.output[11].c_str());
}

void testStatusCommandPrintsNothingWhenUnplugged()
{
  CommandStack stack;
  stack.serial.plugged = false;
  const size_t before = stack.serial.output.size();
  stack.serial.feed("status\n");
  stack.controller.update();
  TEST_ASSERT_EQUAL(before, stack.serial.output.size());
}

void testStatusCommandRestartsSnapshotInterval()
{
  CommandStack stack;
  stack.clock.advance(1000);
  stack.reporter.update();
  const size_t afterPeriodic = stack.serial.output.size();
  TEST_ASSERT_EQUAL(7, afterPeriodic);

  stack.clock.advance(400);
  stack.serial.feed("status\n");
  stack.controller.update();
  TEST_ASSERT_EQUAL(afterPeriodic + 7, stack.serial.output.size());

  stack.clock.advance(999);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(afterPeriodic + 7, stack.serial.output.size());

  stack.clock.advance(1);
  stack.reporter.update();
  TEST_ASSERT_EQUAL(afterPeriodic + 14, stack.serial.output.size());
}

void testSetStatusRejectsUnknownValue()
{
  CommandStack stack;
  stack.serial.feed("set status maybe\napply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("ERR status", stack.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output[1].c_str());
  TEST_ASSERT_EQUAL(0, stack.store.saveCallCount);
  TEST_ASSERT_TRUE(stack.reporter.reportingEnabled());
}

void testHostnameStaysStagedUntilApply()
{
  CommandStack stack;
  const int begins = stack.wifi.beginCallCount;
  const int resets = stack.wifi.resetStationModeCount;
  const int names = stack.wifi.setHostnameCount;
  const int mqttDisconnects = stack.mqttClient.disconnectCallCount;
  const size_t events = stack.wifi.calls.size();

  stack.serial.feed("set wifi.hostname plant-room\n");
  stack.controller.update();
  TEST_ASSERT_EQUAL_STRING("OK staged", stack.serial.output.back().c_str());
  TEST_ASSERT_EQUAL(begins, stack.wifi.beginCallCount);
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           stack.runtime.config().wifiHostname);

  stack.serial.feed("apply\n");
  stack.controller.update();
  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output.back().c_str());
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.runtime.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.store.wifiHostname().c_str());
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.wifi.lastHostname.c_str());
  TEST_ASSERT_TRUE(stack.store.lastFields.wifiHostname);
  TEST_ASSERT_FALSE(stack.store.lastFields.wifiPassword);
  TEST_ASSERT_FALSE(stack.store.lastFields.statusReporting);
  TEST_ASSERT_FALSE(stack.store.lastFields.mqttHost);
  TEST_ASSERT_EQUAL(resets + 1, stack.wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL(names + 1, stack.wifi.setHostnameCount);
  TEST_ASSERT_EQUAL(begins + 1, stack.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(mqttDisconnects, stack.mqttClient.disconnectCallCount);
  TEST_ASSERT_EQUAL_STRING("reset", stack.wifi.calls[events].c_str());
  TEST_ASSERT_EQUAL_STRING("hostname", stack.wifi.calls[events + 1].c_str());
  TEST_ASSERT_EQUAL_STRING("begin", stack.wifi.calls[events + 2].c_str());
}

void testInvalidHostnameIsRejectedBeforeStaging()
{
  CommandStack stack;
  const int begins = stack.wifi.beginCallCount;
  const std::string tooLong(32, 'a');
  stack.serial.feed("set wifi.hostname -bad\n");
  stack.serial.feed("set wifi.hostname bad_name\n");
  stack.serial.feed("set wifi.hostname " + tooLong + "\n");
  stack.serial.feed("set wifi.hostname \n");
  stack.serial.feed("apply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("ERR hostname", stack.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("ERR hostname", stack.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("ERR hostname", stack.serial.output[2].c_str());
  TEST_ASSERT_EQUAL_STRING("ERR hostname", stack.serial.output[3].c_str());
  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output[4].c_str());
  TEST_ASSERT_EQUAL(0, stack.store.saveCallCount);
  TEST_ASSERT_EQUAL(begins, stack.wifi.beginCallCount);
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           stack.runtime.config().wifiHostname);
}

void testFailedApplyKeepsHostnameAndStatusUntilRetry()
{
  CommandStack stack;
  stack.store.saveResult = false;
  const int begins = stack.wifi.beginCallCount;
  stack.serial.feed("set status off\nset wifi.hostname plant-room\napply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("ERR apply", stack.serial.output.back().c_str());
  TEST_ASSERT_TRUE(stack.reporter.reportingEnabled());
  TEST_ASSERT_TRUE(stack.runtime.config().statusReporting);
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           stack.runtime.config().wifiHostname);
  TEST_ASSERT_EQUAL(begins, stack.wifi.beginCallCount);

  stack.store.saveResult = true;
  stack.serial.feed("apply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("OK applied", stack.serial.output.back().c_str());
  TEST_ASSERT_FALSE(stack.reporter.reportingEnabled());
  TEST_ASSERT_FALSE(stack.store.statusReporting());
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.runtime.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.store.wifiHostname().c_str());
  TEST_ASSERT_EQUAL(begins + 1, stack.wifi.beginCallCount);
  TEST_ASSERT_TRUE(stack.store.lastFields.wifiHostname);
  TEST_ASSERT_TRUE(stack.store.lastFields.statusReporting);
}

void testPasswordApplyReconnectsWithStoredHostname()
{
  CommandStack stack;
  stack.serial.feed("set wifi.hostname plant-room\napply\n");
  stack.controller.update();
  const int resets = stack.wifi.resetStationModeCount;
  const int names = stack.wifi.setHostnameCount;
  const int begins = stack.wifi.beginCallCount;
  const int mqttDisconnects = stack.mqttClient.disconnectCallCount;

  stack.serial.feed("set wifi.password new-password\napply\n");
  stack.controller.update();

  TEST_ASSERT_EQUAL_STRING("new-password",
                           stack.runtime.config().wifiPassword);
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.runtime.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.store.wifiHostname().c_str());
  TEST_ASSERT_EQUAL_STRING("new-password", stack.store.wifiPassword().c_str());
  TEST_ASSERT_TRUE(stack.store.lastFields.wifiPassword);
  TEST_ASSERT_FALSE(stack.store.lastFields.wifiHostname);
  TEST_ASSERT_EQUAL(resets, stack.wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL(names, stack.wifi.setHostnameCount);
  TEST_ASSERT_EQUAL(begins + 1, stack.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(mqttDisconnects, stack.mqttClient.disconnectCallCount);
  TEST_ASSERT_EQUAL_STRING("plant-room", stack.wifi.lastHostname.c_str());
  TEST_ASSERT_EQUAL_STRING("new-password", stack.wifi.lastPassword.c_str());
}

void testRecordKeepsDefaultHostnameAndStatusWhenMissing()
{
  FakePreferenceStore store;
  store.strings[NetworkConfigKeys::wifiSsid] = "garden";
  NetworkConfig defaults = {"", "", "", 1883, "watering-controller", nullptr,
                            nullptr, "watering-controller", true};
  NetworkConfigData data;

  TEST_ASSERT_TRUE(NetworkConfigRecord::load(store, defaults, data));
  TEST_ASSERT_EQUAL_STRING("watering-controller", data.wifiHostname.c_str());
  TEST_ASSERT_TRUE(data.statusReporting);
  TEST_ASSERT_EQUAL(0, store.writeCounts[NetworkConfigKeys::wifiHostname]);
  TEST_ASSERT_EQUAL(0, store.writeCounts[NetworkConfigKeys::statusReport]);
  TEST_ASSERT_FALSE(store.contains(NetworkConfigKeys::wifiHostname));
  TEST_ASSERT_FALSE(store.contains(NetworkConfigKeys::statusReport));
}

void testRecordLoadsStoredHostnameAndStatus()
{
  FakePreferenceStore store;
  store.strings[NetworkConfigKeys::wifiSsid] = "garden";
  store.strings[NetworkConfigKeys::wifiHostname] = "plant-room";
  store.ports[NetworkConfigKeys::statusReport] = 0;
  NetworkConfig defaults = {"", "", "", 1883, "watering-controller", nullptr,
                            nullptr, "watering-controller", true};
  NetworkConfigData data;

  TEST_ASSERT_TRUE(NetworkConfigRecord::load(store, defaults, data));
  TEST_ASSERT_EQUAL_STRING("plant-room", data.wifiHostname.c_str());
  TEST_ASSERT_FALSE(data.statusReporting);
}

void testRecordSavesHostnameAndStatusOnly()
{
  FakePreferenceStore store;
  NetworkConfig update = {"ignored", "ignored", "ignored", 1883, "ignored",
                          nullptr, nullptr, "plant-room", false};
  NetworkConfigFieldMask fields;
  fields.wifiHostname = true;
  fields.statusReporting = true;

  TEST_ASSERT_TRUE(NetworkConfigRecord::save(store, update, fields));
  TEST_ASSERT_EQUAL_STRING(
    "plant-room", store.strings[NetworkConfigKeys::wifiHostname].c_str());
  TEST_ASSERT_EQUAL(0, store.ports[NetworkConfigKeys::statusReport]);
  TEST_ASSERT_EQUAL(0, store.writeCounts[NetworkConfigKeys::wifiSsid]);
  TEST_ASSERT_EQUAL(1, store.writeCounts[NetworkConfigKeys::wifiHostname]);
  TEST_ASSERT_EQUAL(1, store.writeCounts[NetworkConfigKeys::statusReport]);
}

void testRuntimeRejectsInvalidHostnameWithoutSaving()
{
  FakeNetworkConfigStore store;
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  runtime.begin(config());
  NetworkConfig update = config();
  update.wifiHostname = "-bad";
  NetworkConfigFieldMask fields;
  fields.wifiHostname = true;

  TEST_ASSERT_FALSE(runtime.apply(update, fields));
  TEST_ASSERT_EQUAL(0, store.saveCallCount);
  TEST_ASSERT_EQUAL_STRING("old", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(1, wifi.beginCallCount);
}

void testStoredStatusOffLoadsDisabled()
{
  FakeNetworkConfigStore store;
  store.seed({"garden", "pw", "broker", 1883, "controller", nullptr, nullptr,
              "plant-room", false});
  FakeClock clock;
  EmptyModuleHostFixture modules(clock);
  FakeWifi wifi;
  FakeNtpAdapter ntpAdapter;
  FakeMqttClient client;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(
    ntpAdapter, clock, {"pool.ntp.org", "time.nist.gov", nullptr, 0, 0, 60000});
  MqttService mqtt(client, clock, mqttConfig());
  NetworkRuntime runtime(store, wifiManager, mqtt);
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, mqtt,
                                modules.host);
  const NetworkConfig defaults = {
    "", "", "", 1883, "watering-controller", nullptr, nullptr,
    "watering-controller", true};

  TEST_ASSERT_TRUE(runtime.begin(defaults));
  reporter.begin({1000});
  reporter.setReportingEnabled(runtime.config().statusReporting);

  TEST_ASSERT_FALSE(runtime.config().statusReporting);
  TEST_ASSERT_FALSE(reporter.reportingEnabled());
  TEST_ASSERT_EQUAL_STRING("plant-room", runtime.config().wifiHostname);
  clock.advance(1000);
  reporter.update();
  TEST_ASSERT_EQUAL(0, serial.output.size());
}

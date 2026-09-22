#include <unity.h>

#include "NetworkConfigRecord.h"
#include "NetworkRuntime.h"
#include "SerialConfigController.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNetworkConfigStore.h"
#include "fakes/FakePreferenceStore.h"
#include "fakes/FakeSerialPort.h"
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
  SerialConfigController controller(serial, runtime);

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

  NetworkConfig replacement = {"new", "pw", "host", 1883, "id", nullptr, nullptr};
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
  SerialConfigController controller(serial, runtime);

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
  SerialConfigController controller(serial, runtime);

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
  SerialConfigController controller(serial, runtime);
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
  SerialConfigController controller(serial, runtime);
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

#include <unity.h>

#include "NetworkRuntime.h"
#include "SerialConfigController.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNetworkConfigStore.h"
#include "fakes/FakeSerialPort.h"
#include "fakes/FakeUsbVbus.h"
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
  store.loadResult = true;
  store.stored = {"saved", "pw", "broker", 1884, "id", "u", "p"};
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

void testSerialStagesUntilApplyAndGatesUsb()
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
  FakeUsbVbus usb;
  SerialConfigController controller(serial, usb, runtime);

  serial.feed("set wifi.ssid new-network\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("old", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(0, store.saveCallCount);

  serial.feed("apply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("new-network", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(1, store.saveCallCount);

  usb.present = false;
  serial.feed("set mqtt.host hidden\napply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("new-network", runtime.config().wifiSsid);
  TEST_ASSERT_EQUAL(1, store.saveCallCount);
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
  TEST_ASSERT_FALSE(runtime.apply(replacement));
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
  FakeUsbVbus usb;
  SerialConfigController controller(serial, usb, runtime);

  serial.feed("set wifi.ssid applied-network\napply\n");
  controller.update();
  TEST_ASSERT_EQUAL_STRING("applied-network", runtime.config().wifiSsid);

  serial.feed("set wifi.ssid staged-only\n");
  controller.update();

  TEST_ASSERT_EQUAL_STRING("applied-network", runtime.config().wifiSsid);
}

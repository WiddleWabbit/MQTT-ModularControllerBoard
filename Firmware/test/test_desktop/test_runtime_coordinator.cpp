#include <unity.h>

#include "MqttManager.h"
#include "RuntimeCoordinator.h"
#include "NtpHandler.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNtpClient.h"
#include "fakes/FakeWifiStation.h"

namespace {
FakeClock fakeClock;
FakeWifiStation wifi;
FakeNtpClient ntp;
FakeMqttClient mqtt;
MqttConfig mqttConfig;

void resetFixtures()
{
  fakeClock.currentMillis = 0;
  wifi.connected = false;
  ntp.reset();
  mqtt.reset();
  mqttConfig = {"broker.example", 1883, "controller-1", "", "", 1000};
}
}

/**
 * Verifies that one runtime update advances both network-dependent services.
 *
 * @return Nothing.
 */
void test_runtime_updates_ntp_and_mqtt_services()
{
  resetFixtures();
  wifi.connected = true;
  NtpHandler ntpHandler(wifi, fakeClock, ntp, "time.example", 1000);
  MqttManager mqttManager(wifi, fakeClock, mqtt, mqttConfig);
  RuntimeCoordinator runtime(ntpHandler, mqttManager);

  runtime.update();

  TEST_ASSERT_EQUAL_UINT32(1, ntp.updateCount);
  TEST_ASSERT_EQUAL_UINT32(1, ntp.requestCount);
  TEST_ASSERT_EQUAL_UINT32(1, mqtt.connectCount);
  TEST_ASSERT_TRUE(mqttManager.isConnected());
}

/**
 * Verifies that disconnected WiFi leaves both dependent services safely idle.
 *
 * @return Nothing.
 */
void test_runtime_does_not_attempt_network_services_without_wifi()
{
  resetFixtures();
  NtpHandler ntpHandler(wifi, fakeClock, ntp, "time.example", 1000);
  MqttManager mqttManager(wifi, fakeClock, mqtt, mqttConfig);
  RuntimeCoordinator runtime(ntpHandler, mqttManager);

  runtime.update();

  TEST_ASSERT_EQUAL_UINT32(0, ntp.updateCount);
  TEST_ASSERT_EQUAL_UINT32(0, ntp.requestCount);
  TEST_ASSERT_EQUAL_UINT32(0, mqtt.connectCount);
  TEST_ASSERT_FALSE(mqttManager.isConnected());
}

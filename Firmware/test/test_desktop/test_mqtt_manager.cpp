#include <unity.h>

#include "MqttManager.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeWifiStation.h"

namespace {
FakeClock clock;
FakeWifiStation wifi;
FakeMqttClient mqtt;
MqttConfig config;
const char* receivedTopic = nullptr;
std::string receivedPayload;
unsigned int receivedCount = 0;

void messageHandler(const char* topic, const char* payload,
                    unsigned int payloadLength)
{
  receivedTopic = topic;
  receivedPayload.assign(payload, payloadLength);
  ++receivedCount;
}

void resetFixtures()
{
  clock.currentMillis = 0;
  wifi.connected = false;
  mqtt.reset();
  config = {"broker.example", 1883, "controller-1", "user", "secret", 1000};
  receivedTopic = nullptr;
  receivedPayload.clear();
  receivedCount = 0;
}
}

void test_mqtt_defaultsToDisconnectedWaitingForWifi()
{
  resetFixtures();
  MqttManager manager(wifi, clock, mqtt, config);

  TEST_ASSERT_EQUAL_INT(MqttConnectionState::WaitingForWifi, manager.state());
  TEST_ASSERT_FALSE(manager.isConnected());
  manager.update();
  TEST_ASSERT_EQUAL_UINT32(0, mqtt.connectCount);
  TEST_ASSERT_EQUAL_UINT32(0, mqtt.loopCount);
}

void test_mqtt_connectsWhenWifiIsAvailable()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);

  manager.update();

  TEST_ASSERT_EQUAL_UINT32(1, mqtt.connectCount);
  TEST_ASSERT_EQUAL_STRING("controller-1", mqtt.lastClientId.c_str());
  TEST_ASSERT_EQUAL_STRING("user", mqtt.lastUsername.c_str());
  TEST_ASSERT_EQUAL_STRING("secret", mqtt.lastPassword.c_str());
  TEST_ASSERT_EQUAL_INT(MqttConnectionState::Connected, manager.state());
}

void test_mqtt_subscribesConfiguredTopicsAfterConnecting()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);

  TEST_ASSERT_TRUE(manager.addSubscription("controller/command"));
  TEST_ASSERT_TRUE(manager.addSubscription("controller/config"));
  manager.update();

  TEST_ASSERT_EQUAL_UINT32(2, mqtt.subscribeCount);
  TEST_ASSERT_EQUAL_STRING("controller/command",
                           mqtt.subscribedTopics[0].c_str());
  TEST_ASSERT_EQUAL_STRING("controller/config",
                           mqtt.subscribedTopics[1].c_str());
}

void test_mqtt_retriesAfterFailedConnectionAtConfiguredInterval()
{
  resetFixtures();
  wifi.connected = true;
  mqtt.connectResult = false;
  MqttManager manager(wifi, clock, mqtt, config);

  manager.update();
  clock.advance(999);
  manager.update();
  TEST_ASSERT_EQUAL_UINT32(1, mqtt.connectCount);
  clock.advance(1);
  manager.update();
  TEST_ASSERT_EQUAL_UINT32(2, mqtt.connectCount);
  TEST_ASSERT_EQUAL_INT(MqttConnectionState::Backoff, manager.state());
}

void test_mqtt_reconnectsImmediatelyAfterDisconnect()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.update();
  mqtt.connected = false;
  clock.advance(1000);
  manager.update();

  TEST_ASSERT_EQUAL_UINT32(2, mqtt.connectCount);
}

void test_mqtt_disconnectsWhenWifiIsLost()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.update();
  wifi.connected = false;
  manager.update();

  TEST_ASSERT_EQUAL_UINT32(1, mqtt.disconnectCount);
  TEST_ASSERT_EQUAL_INT(MqttConnectionState::WaitingForWifi, manager.state());
}

void test_mqtt_loopRunsOnlyWhileConnected()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.update();
  manager.update();
  wifi.connected = false;
  manager.update();

  TEST_ASSERT_EQUAL_UINT32(1, mqtt.loopCount);
}

void test_mqtt_publishRequiresConnection()
{
  resetFixtures();
  MqttManager manager(wifi, clock, mqtt, config);

  TEST_ASSERT_FALSE(manager.publish("a/topic", "payload", false));
  TEST_ASSERT_EQUAL_UINT32(0, mqtt.publishCount);
  wifi.connected = true;
  manager.update();
  TEST_ASSERT_TRUE(manager.publish("a/topic", "payload", true));
  TEST_ASSERT_EQUAL_UINT32(1, mqtt.publishCount);
  TEST_ASSERT_TRUE(mqtt.published[0].retained);
}

void test_mqtt_publishFailureIsReturned()
{
  resetFixtures();
  wifi.connected = true;
  mqtt.publishResult = false;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.update();

  TEST_ASSERT_FALSE(manager.publish("a/topic", "payload", false));
}

void test_mqtt_routesIncomingMessages()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.setMessageHandler(messageHandler);
  manager.update();

  mqtt.deliver("controller/command", "ON");

  TEST_ASSERT_EQUAL_UINT32(1, receivedCount);
  TEST_ASSERT_EQUAL_STRING("controller/command", receivedTopic);
  TEST_ASSERT_EQUAL_STRING("ON", receivedPayload.c_str());
}

void test_mqtt_preservesBinaryPayloadLength()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.setMessageHandler(messageHandler);
  manager.update();
  const char binaryPayload[] = {'A', '\0', 'B'};

  mqtt.messageCallback(mqtt.callbackContext, "binary",
                       reinterpret_cast<const unsigned char*>(binaryPayload),
                       sizeof(binaryPayload));

  TEST_ASSERT_EQUAL_UINT32(1, receivedCount);
  TEST_ASSERT_EQUAL_UINT32(3, receivedPayload.size());
  TEST_ASSERT_EQUAL_INT('B', receivedPayload[2]);
}

void test_mqtt_rejectsInvalidConfigurationAndTopics()
{
  resetFixtures();
  MqttConfig invalid = config;
  invalid.host = "";
  TEST_ASSERT_FALSE(MqttManager::isValidConfiguration(invalid));
  MqttManager manager(wifi, clock, mqtt, config);

  TEST_ASSERT_FALSE(manager.addSubscription(nullptr));
  TEST_ASSERT_FALSE(manager.addSubscription(""));
  TEST_ASSERT_FALSE(manager.setMessageHandler(nullptr));
}

void test_mqtt_rejectsSubscriptionFailureAndRetries()
{
  resetFixtures();
  wifi.connected = true;
  mqtt.subscribeResult = false;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.addSubscription("topic");

  TEST_ASSERT_FALSE(manager.update());
  TEST_ASSERT_EQUAL_INT(MqttConnectionState::Backoff, manager.state());
  clock.advance(1000);
  mqtt.subscribeResult = true;
  TEST_ASSERT_TRUE(manager.update());
  TEST_ASSERT_EQUAL_INT(MqttConnectionState::Connected, manager.state());
}

void test_mqtt_subscribesTopicAddedWhileConnected()
{
  resetFixtures();
  wifi.connected = true;
  MqttManager manager(wifi, clock, mqtt, config);
  manager.update();

  TEST_ASSERT_TRUE(manager.addSubscription("controller/live"));
  TEST_ASSERT_EQUAL_UINT32(1, mqtt.subscribeCount);
  TEST_ASSERT_EQUAL_STRING("controller/live",
                           mqtt.subscribedTopics[0].c_str());
}

void test_mqtt_fakeSupportsSequencedConnectionResults()
{
  resetFixtures();
  wifi.connected = true;
  mqtt.connectResults.push_back(false);
  mqtt.connectResults.push_back(true);
  MqttManager manager(wifi, clock, mqtt, config);

  TEST_ASSERT_FALSE(manager.update());
  clock.advance(config.reconnectIntervalMs);
  TEST_ASSERT_TRUE(manager.update());
  TEST_ASSERT_EQUAL_UINT32(2, mqtt.connectCount);
}

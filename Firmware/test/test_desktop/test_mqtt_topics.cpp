#include <string>

#include <unity.h>

#include "MqttTopicLayout.h"

void testMqttTopicLayoutBuildsPlantRoomTopics()
{
  MqttTopicLayout topics("plant-room");
  TEST_ASSERT_EQUAL_STRING("plant-room", topics.deviceId());
  TEST_ASSERT_EQUAL(1, topics.generation());
  TEST_ASSERT_EQUAL_STRING("plant-room/slot", topics.slotPrefix());
  TEST_ASSERT_EQUAL_STRING("plant-room/solenoids", topics.solenoidCommand());
  TEST_ASSERT_EQUAL_STRING("plant-room/solenoids/connected",
                           topics.solenoidConnected());
  TEST_ASSERT_EQUAL_STRING("plant-room/pump", topics.pumpCommand());
  TEST_ASSERT_EQUAL_STRING("plant-room/sensor/read", topics.sensorRead());
  TEST_ASSERT_EQUAL(MqttTopicLayout::kSubscriptionCount,
                    topics.subscriptionCount());

  const MqttSubscription* subscriptions = topics.subscriptions();
  TEST_ASSERT_EQUAL_STRING("plant-room/solenoids", subscriptions[0].topic);
  TEST_ASSERT_EQUAL(1, subscriptions[0].qos);
  TEST_ASSERT_EQUAL_STRING("plant-room/solenoids/connected",
                           subscriptions[1].topic);
  TEST_ASSERT_EQUAL(1, subscriptions[1].qos);
  TEST_ASSERT_EQUAL_STRING("plant-room/pump", subscriptions[2].topic);
  TEST_ASSERT_EQUAL(1, subscriptions[2].qos);
  TEST_ASSERT_EQUAL_STRING("plant-room/sensor/read", subscriptions[3].topic);
  TEST_ASSERT_EQUAL(1, subscriptions[3].qos);
}

void testMqttTopicLayoutSetDeviceIdBumpsGenerationOnce()
{
  const std::string id(31, 'b');
  MqttTopicLayout topics(id.c_str());
  const std::string slot = id + "/slot";
  TEST_ASSERT_EQUAL_STRING(slot.c_str(), topics.slotPrefix());
  TEST_ASSERT_EQUAL(1, topics.generation());

  topics.setDeviceId(id.c_str());
  TEST_ASSERT_EQUAL(1, topics.generation());
  TEST_ASSERT_EQUAL_STRING(id.c_str(), topics.deviceId());

  topics.setDeviceId("shed");
  TEST_ASSERT_EQUAL(2, topics.generation());
  TEST_ASSERT_EQUAL_STRING("shed", topics.deviceId());
  TEST_ASSERT_EQUAL_STRING("shed/slot", topics.slotPrefix());
  TEST_ASSERT_EQUAL_STRING("shed/solenoids", topics.solenoidCommand());
  TEST_ASSERT_EQUAL_STRING("shed/solenoids/connected",
                           topics.solenoidConnected());
  TEST_ASSERT_EQUAL_STRING("shed/pump", topics.pumpCommand());
  TEST_ASSERT_EQUAL_STRING("shed/sensor/read", topics.sensorRead());
  TEST_ASSERT_EQUAL_STRING("shed/solenoids",
                           topics.subscriptions()[0].topic);
}

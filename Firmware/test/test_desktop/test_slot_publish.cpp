#include <unity.h>

#include "ControllerInfoPublisher.h"
#include "ModuleSlotPublisher.h"
#include "MqttService.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeModuleDevice.h"
#include "fakes/FakeMqttClient.h"

namespace
{

/**
 * Returns a broker config with a short reconnect delay.
 *
 * @return MQTT service configuration.
 */
MqttConfig publisherMqttConfig()
{
  static const MqttSubscription subscriptions[] = {
    {"watering/pump", 1},
  };
  return {"watering-controller", nullptr, nullptr, subscriptions, 1, 100, 400};
}

/**
 * Host, broker, and publisher wired the way loop() calls them.
 */
struct SlotPublishHarness
{
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeMqttClient client;
  MqttService mqtt;
  MqttTopicLayout topics;
  ModuleSlotPublisher publisher;

  /**
   * Creates an empty host and a publisher on watering/slot/N.
   */
  SlotPublishHarness()
    : modules(clock),
      mqtt(client, clock, publisherMqttConfig()),
      topics("watering"),
      publisher(modules.host, mqtt, topics)
  {
  }

  /**
   * Connects the MQTT service to the fake broker.
   *
   * @return Nothing.
   */
  void connect()
  {
    mqtt.setBroker("broker.local", 1883);
    mqtt.begin();
    mqtt.update(true);
  }
};

/**
 * Advances the host clock without calling the publisher.
 *
 * @param harness Connected host harness.
 * @param milliseconds Duration to pump.
 * @return Nothing.
 */
void pumpHost(SlotPublishHarness& harness, uint32_t milliseconds)
{
  for (uint32_t i = 0; i < milliseconds; ++i)
  {
    harness.clock.advance(1);
    harness.modules.host.update();
  }
}

/**
 * Reports whether a retained payload was published on a topic.
 *
 * @param client Fake broker.
 * @param topic Expected topic.
 * @param payload Expected payload.
 * @return True when a matching retained message was recorded.
 */
bool publishedRetained(const FakeMqttClient& client, const char* topic,
                       const char* payload)
{
  for (size_t i = 0; i < client.publishedMessages.size(); ++i)
  {
    const FakeMqttClient::PublishedMessage& message =
      client.publishedMessages[i];
    if (message.topic == topic && message.payload == payload &&
        message.retained)
    {
      return true;
    }
  }
  return false;
}

/**
 * Counts recorded publications of one topic.
 *
 * @param client Fake broker.
 * @param topic Topic name.
 * @return Matching publication count.
 */
int countTopic(const FakeMqttClient& client, const char* topic)
{
  int count = 0;
  for (size_t i = 0; i < client.publishedMessages.size(); ++i)
  {
    if (client.publishedMessages[i].topic == topic)
    {
      ++count;
    }
  }
  return count;
}

}

void testSlotPublisherPublishesRetainedEmptySlots()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();

  harness.publisher.update();

  TEST_ASSERT_EQUAL(4, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/2",
                                      "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/3",
                                      "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/4",
                                      "Empty"));

  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());
}

void testSlotPublisherWaitsUntilMqttConnected()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  harness.modules.host.update();
  TEST_ASSERT_EQUAL(SlotState::Debouncing, harness.modules.host.state(0));

  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishCallCount);

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_EQUAL(4, harness.client.publishedMessages.size());
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Debouncing"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/2",
                                      "Empty"));
}

void testSlotPublisherPublishesLatestSnapshotAfterReconnect()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(4, harness.client.publishedMessages.size());

  harness.client.connectedState = false;
  harness.mqtt.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Backoff, harness.mqtt.state());

  harness.modules.sense(0).setPresent(true);
  harness.modules.host.update();
  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());

  harness.clock.advance(100);
  harness.mqtt.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Connected, harness.mqtt.state());
  harness.publisher.update();

  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Debouncing"));
}

void testSlotPublisherRetriesRejectedSlotOnly()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();
  harness.client.publishResults.push_back(true);
  harness.client.publishResults.push_back(true);
  harness.client.publishResults.push_back(false);
  harness.client.publishResults.push_back(true);

  harness.publisher.update();
  TEST_ASSERT_EQUAL(4, harness.client.publishedMessages.size());

  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/3",
                                      "Empty"));

  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());
}

void testSlotPublisherPublishesOnlyTheChangedSlot()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();
  harness.publisher.update();
  harness.client.publishedMessages.clear();

  harness.modules.sense(0).setPresent(true);
  harness.modules.host.update();
  harness.publisher.update();

  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Debouncing"));

  harness.client.publishedMessages.clear();
  pumpHost(harness, 50);
  TEST_ASSERT_EQUAL(SlotState::Enumerating, harness.modules.host.state(0));
  harness.publisher.update();

  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Enumerating"));
}

void testSlotPublisherPublishesOnlineIdentityEcho()
{
  SlotPublishHarness harness;
  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  harness.modules.bus.attach(device);
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_TRUE(publishedRetained(
    harness.client, "watering/slot/1", "Online IdentityEcho addr=0x10 board 0 firmware 1"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/4",
                                      "Empty"));
}

void testSlotPublisherPublishesUnsupportedType()
{
  SlotPublishHarness harness;
  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  device.typeId = 0x02AA;
  harness.modules.bus.attach(device);
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, harness.modules.host.state(0));

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_TRUE(publishedRetained(
    harness.client, "watering/slot/1",
    "Unsupported type=0x02AA addr=0x10 board 0 firmware 1"));
}

void testSlotPublisherPublishesDaughterRevision()
{
  SlotPublishHarness harness;
  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  device.firmwareVersion = 0x0201;
  harness.modules.bus.attach(device);
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_TRUE(publishedRetained(
    harness.client, "watering/slot/1",
    "Online IdentityEcho addr=0x10 board 2 firmware 1"));
}

void testSlotPublisherPublishesUnsupportedRevision()
{
  SlotPublishHarness harness;
  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  device.typeId = 0x02AA;
  device.firmwareVersion = 0x0102;
  harness.modules.bus.attach(device);
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, harness.modules.host.state(0));

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_TRUE(publishedRetained(
    harness.client, "watering/slot/1",
    "Unsupported type=0x02AA addr=0x10 board 1 firmware 2"));
}

/**
 * Broker, topics, and the controller-identity publisher.
 */
struct InfoHarness
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService mqtt;
  MqttTopicLayout topics;
  ControllerInfoPublisher publisher;

  /**
   * Creates a publisher for the supplied revisions on watering.
   *
   * @param board Board revision.
   * @param firmware Firmware revision.
   */
  InfoHarness(uint8_t board, uint8_t firmware)
    : mqtt(client, clock, publisherMqttConfig()),
      topics("watering"),
      publisher(mqtt, topics, {board, firmware})
  {
  }

  /**
   * Connects the MQTT service to the fake broker.
   *
   * @return Nothing.
   */
  void connect()
  {
    mqtt.setBroker("broker.local", 1883);
    mqtt.begin();
    mqtt.update(true);
  }
};

void testControllerInfoWaitsUntilMqttConnected()
{
  InfoHarness harness(1, 8);
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());
}

void testControllerInfoPublishesRetainedOnce()
{
  InfoHarness harness(1, 8);
  harness.connect();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/controller",
                                      "board 1 firmware 8"));

  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
}

void testControllerInfoUsesConstructorRevisions()
{
  InfoHarness harness(2, 3);
  harness.connect();
  harness.publisher.update();
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/controller",
                                      "board 2 firmware 3"));
}

void testControllerInfoRetriesRejectedPublish()
{
  InfoHarness harness(1, 8);
  harness.connect();
  harness.client.publishResults.push_back(false);
  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());

  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/controller",
                                      "board 1 firmware 8"));

  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
}

void testControllerInfoRepublishesWhenDeviceIdChanges()
{
  InfoHarness harness(1, 8);
  harness.connect();
  harness.publisher.update();
  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());

  harness.topics.setDeviceId("shed");
  harness.publisher.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "shed/controller",
                                      "board 1 firmware 8"));
}

void testSlotPublisherPublishesFaultNack()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Fault, harness.modules.host.state(0));
  TEST_ASSERT_EQUAL(SlotFault::Nack, harness.modules.host.fault(0));

  harness.connect();
  harness.publisher.update();

  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Fault Nack"));
}

void testSlotPublisherRepublishesEmptyAfterUnplug()
{
  SlotPublishHarness harness;
  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  harness.modules.bus.attach(device);
  harness.modules.host.begin();
  harness.connect();
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  harness.publisher.update();
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));

  harness.client.publishedMessages.clear();
  harness.modules.sense(0).setPresent(false);
  pumpHost(harness, 60);
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  harness.publisher.update();

  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Empty"));
}

void testSlotPublisherDoesNotTouchTheBus()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();
  const size_t beforeEmpty = harness.modules.bus.protocolOpCount();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(beforeEmpty, harness.modules.bus.protocolOpCount());

  FakeModuleDevice device(harness.clock, harness.modules.mod(0));
  harness.modules.bus.attach(device);
  harness.modules.sense(0).setPresent(true);
  pumpHost(harness, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  const size_t beforeOnline = harness.modules.bus.protocolOpCount();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(beforeOnline, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_TRUE(publishedRetained(
    harness.client, "watering/slot/1", "Online IdentityEcho addr=0x10 board 0 firmware 1"));
}

void testSlotPublisherRepublishesWhenDeviceIdChanges()
{
  SlotPublishHarness harness;
  harness.modules.host.begin();
  harness.connect();
  harness.publisher.update();
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "watering/slot/1",
                                      "Empty"));
  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());

  harness.topics.setDeviceId("shed");
  harness.publisher.update();
  TEST_ASSERT_EQUAL(4, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "shed/slot/1", "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "shed/slot/2", "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "shed/slot/3", "Empty"));
  TEST_ASSERT_TRUE(publishedRetained(harness.client, "shed/slot/4", "Empty"));

  harness.client.publishedMessages.clear();
  harness.publisher.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());
}


// ========== Runner ==========

/**
 * Registers the retained slot-status cases.
 *
 * @return Nothing.
 */
void runSlotPublishTests()
{
  // UNITY_BEGIN records test_main.cpp, so name this file for the report.
  UnitySetTestFile(__FILE__);
  RUN_TEST(testSlotPublisherPublishesRetainedEmptySlots);
  RUN_TEST(testSlotPublisherWaitsUntilMqttConnected);
  RUN_TEST(testSlotPublisherPublishesLatestSnapshotAfterReconnect);
  RUN_TEST(testSlotPublisherRetriesRejectedSlotOnly);
  RUN_TEST(testSlotPublisherPublishesOnlyTheChangedSlot);
  RUN_TEST(testSlotPublisherPublishesOnlineIdentityEcho);
  RUN_TEST(testSlotPublisherPublishesDaughterRevision);
  RUN_TEST(testSlotPublisherPublishesUnsupportedType);
  RUN_TEST(testSlotPublisherPublishesUnsupportedRevision);
  RUN_TEST(testSlotPublisherPublishesFaultNack);
  RUN_TEST(testControllerInfoWaitsUntilMqttConnected);
  RUN_TEST(testControllerInfoPublishesRetainedOnce);
  RUN_TEST(testControllerInfoUsesConstructorRevisions);
  RUN_TEST(testControllerInfoRetriesRejectedPublish);
  RUN_TEST(testControllerInfoRepublishesWhenDeviceIdChanges);
  RUN_TEST(testSlotPublisherRepublishesEmptyAfterUnplug);
  RUN_TEST(testSlotPublisherDoesNotTouchTheBus);
  RUN_TEST(testSlotPublisherRepublishesWhenDeviceIdChanges);
}

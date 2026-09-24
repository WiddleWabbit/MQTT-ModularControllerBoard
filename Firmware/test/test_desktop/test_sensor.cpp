#include <unity.h>

#include <cstring>

#include "ModuleCodec.h"
#include "ModuleProtocol.h"
#include "SensorMqttBridge.h"
#include "SensorPoller.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeModuleDevice.h"
#include "fakes/FakeMqttClient.h"

namespace
{
/**
 * Advances the clock one millisecond at a time and services the host.
 *
 * @param fixture Host fixture.
 * @param milliseconds Duration to pump.
 * @return Nothing.
 */
void pumpMs(EmptyModuleHostFixture& fixture, uint32_t milliseconds)
{
  for (uint32_t i = 0; i < milliseconds; ++i)
  {
    fixture.clock.advance(1);
    fixture.host.update();
  }
}

/**
 * Seats a module and pumps until enumeration can finish.
 *
 * @param fixture Host fixture.
 * @param slotIndex Firmware slot.
 * @param device Simulated module.
 * @param timeoutMs Pump timeout.
 * @return Nothing.
 */
void plugAndPump(EmptyModuleHostFixture& fixture, uint8_t slotIndex,
                 FakeModuleDevice& device, uint32_t timeoutMs = 800)
{
  fixture.bus.attach(device);
  fixture.sense(slotIndex).setPresent(true);
  pumpMs(fixture, timeoutMs);
}

/**
 * Reads the command byte of the latest bus transaction.
 *
 * @param fixture Host fixture.
 * @return Command byte, or 0 when the frame is short.
 */
uint8_t lastCommand(const EmptyModuleHostFixture& fixture)
{
  const FakeI2cOp& op = fixture.bus.ops.back();
  return op.tx.size() > 1 ? op.tx[1] : 0;
}

/**
 * Reads the sensor index byte of the latest request.
 *
 * @param fixture Host fixture.
 * @return Index byte, or 0xFF when the frame has no payload.
 */
uint8_t lastSensorIndex(const EmptyModuleHostFixture& fixture)
{
  const FakeI2cOp& op = fixture.bus.ops.back();
  return op.tx.size() > 2 ? op.tx[2] : 0xFF;
}

/**
 * Returns a broker config subscribed to the sensor read command.
 *
 * @return MQTT service configuration.
 */
MqttConfig sensorMqttConfig()
{
  static const MqttSubscription subscriptions[] = {
    {kSensorReadTopic, 1},
  };
  return {"watering-controller", nullptr, nullptr, subscriptions, 1, 100, 400};
}

/**
 * Sensor module, poller, and MQTT bridge wired the way loop() calls them.
 */
struct SensorHarness
{
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeModuleDevice device;
  FakeMqttClient client;
  MqttService mqtt;
  SensorPoller poller;
  SensorMqttBridge bridge;

  /**
   * Creates an online-ready sensor harness with MQTT connected.
   */
  SensorHarness()
    : modules(clock),
      device(clock, modules.mod1),
      mqtt(client, clock, sensorMqttConfig()),
      poller(modules.host, clock),
      bridge(poller, mqtt, "watering/slot")
  {
    device.typeId = module_protocol::kTypeSensorModule;
    modules.host.begin();
    mqtt.setBroker("broker.local", 1883);
    mqtt.begin();
    mqtt.update(true);
    mqtt.setMessageHandler(SensorMqttBridge::onMqttMessage, &bridge);
  }
};

/**
 * Reports whether a payload was published on a topic.
 *
 * @param client Fake broker.
 * @param topic Expected topic.
 * @param payload Expected payload.
 * @param retained Expected retained flag.
 * @return True when a matching message was recorded.
 */
bool published(const FakeMqttClient& client, const char* topic,
               const char* payload, bool retained)
{
  for (size_t i = 0; i < client.publishedMessages.size(); ++i)
  {
    const FakeMqttClient::PublishedMessage& message =
        client.publishedMessages[i];
    if (message.topic == topic && message.payload == payload &&
        message.retained == retained)
    {
      return true;
    }
  }
  return false;
}
}

void testCodecEncodesSensorCommands()
{
  uint8_t tx[8] = {};
  const size_t countSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdGetSensorCount, nullptr, 0, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(3, countSize);
  TEST_ASSERT_EQUAL(2, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 2), tx[2]);

  const uint8_t index = 2;
  const size_t readSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdGetSensorReading, &index, 1, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(4, readSize);
  TEST_ASSERT_EQUAL(3, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading, tx[1]);
  TEST_ASSERT_EQUAL(2, tx[2]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 3), tx[3]);

  uint8_t encoded[4];
  module_protocol::writeInt32Be(encoded, -123456);
  TEST_ASSERT_EQUAL(-123456, module_protocol::readInt32Be(encoded));
}

void testHostReadsSensorCountConnectedAndReading()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 2;
  device.sensorConnected[0] = true;
  device.sensorConnected[1] = false;
  device.sensorValue[0] = -123456;
  device.sensorValue[1] = 40;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL_STRING("Sensor", fixture.host.typeName(0));

  const size_t before = fixture.bus.protocolOpCount();
  const SensorCountResult count = fixture.host.querySensorCount(0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, count.status);
  TEST_ASSERT_EQUAL(2, count.count);
  TEST_ASSERT_EQUAL(before + 1, fixture.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(0x10, fixture.bus.ops.back().address);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount, lastCommand(fixture));

  const SensorConnectedResult presence =
      fixture.host.querySensorConnected(0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, presence.status);
  TEST_ASSERT_FALSE(presence.connected);
  TEST_ASSERT_EQUAL(1, lastSensorIndex(fixture));

  const SensorReadingResult reading = fixture.host.querySensorReading(0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, reading.status);
  TEST_ASSERT_TRUE(reading.connected);
  TEST_ASSERT_EQUAL(-123456, reading.value);

  device.forceBusy = true;
  const SensorReadingResult busy = fixture.host.querySensorReading(0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Busy, busy.status);
}

void testHostRejectsSensorQueryUnlessOnlineSensor()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  const size_t before = fixture.bus.protocolOpCount();
  const SensorCountResult absent = fixture.host.querySensorCount(0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, absent.status);
  TEST_ASSERT_EQUAL(before, fixture.bus.protocolOpCount());

  FakeModuleDevice device(clock, fixture.mod1);
  plugAndPump(fixture, 0, device);
  const size_t online = fixture.bus.protocolOpCount();
  const SensorReadingResult echo = fixture.host.querySensorReading(0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, echo.status);
  TEST_ASSERT_EQUAL(online, fixture.bus.protocolOpCount());

  const SensorConnectedResult outOfRange =
      fixture.host.querySensorConnected(0, module_protocol::kMaxSensorsPerModule);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, outOfRange.status);
  TEST_ASSERT_EQUAL(online, fixture.bus.protocolOpCount());
}

void testHostRejectsShortSensorPayload()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 1;
  device.shortSensorPayload = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  const SensorCountResult count = fixture.host.querySensorCount(0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, count.status);
  const SensorReadingResult reading = fixture.host.querySensorReading(0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, reading.status);
}

void testPollerQueriesCountThenEachSensorAtInterval()
{
  SensorHarness harness;
  harness.device.sensorCount = 2;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorConnected[1] = false;
  harness.device.sensorValue[0] = 10;
  harness.device.sensorValue[1] = -4;
  plugAndPump(harness.modules, 0, harness.device);

  const size_t baseline = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(baseline + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  TEST_ASSERT_TRUE(harness.poller.countKnown(0));
  TEST_ASSERT_EQUAL(2, harness.poller.sensorCount(0));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastSensorIndex(harness.modules));

  bool connected = false;
  bool hasValue = false;
  int32_t value = 0;
  TEST_ASSERT_TRUE(
      harness.poller.sensorSample(0, 0, &connected, &hasValue, &value));
  TEST_ASSERT_TRUE(connected);
  TEST_ASSERT_TRUE(hasValue);
  TEST_ASSERT_EQUAL(10, value);
  TEST_ASSERT_TRUE(
      harness.poller.sensorSample(0, 1, &connected, &hasValue, &value));
  TEST_ASSERT_FALSE(connected);
  TEST_ASSERT_EQUAL(-4, value);

  const size_t afterCycle = harness.modules.bus.protocolOpCount();
  harness.clock.advance(59999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle, harness.modules.bus.protocolOpCount());
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
}

void testPollerRequeriesCountAfterModuleReset()
{
  SensorHarness harness;
  harness.device.sensorCount = 2;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_EQUAL(2, harness.poller.sensorCount(0));
  const uint32_t firstEpoch = harness.modules.host.identityEpoch(0);

  harness.device.sensorCount = 4;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(harness.modules.host.identityEpoch(0) > firstEpoch);

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(4, harness.poller.sensorCount(0));
}

void testPollerIdleWhenSensorCountIsZero()
{
  SensorHarness harness;
  harness.device.sensorCount = 0;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_TRUE(harness.poller.countKnown(0));
  TEST_ASSERT_EQUAL(0, harness.poller.sensorCount(0));
  const size_t afterCount = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  harness.clock.advance(120000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCount, harness.modules.bus.protocolOpCount());
}

void testPollerRetriesBusyThenMovesOn()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.device.forceBusy = true;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    harness.poller.update();
    TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                      lastCommand(harness.modules));
  }
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
}

void testSensorReadCommandPublishesWithoutBusUntilUpdate()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 2500;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  const size_t afterCount = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kSensorReadTopic, "1 1");
  TEST_ASSERT_EQUAL(afterCount, harness.modules.bus.protocolOpCount());

  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCount + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", true));
}

void testSensorReadCommandRepeatsUnchangedReading()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 2500;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  harness.client.publishedMessages.clear();

  harness.client.deliver(kSensorReadTopic, "1 1");
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", true));
}

void testSensorReadCommandRejectsMalformedPayload()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 3;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  const size_t afterCount = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorReadTopic, "1");
  harness.client.deliver(kSensorReadTopic, "nope");
  harness.client.deliver(kSensorReadTopic, "1 2 3");
  harness.client.deliver(kSensorReadTopic, "0 1");
  harness.client.deliver(kSensorReadTopic, "5 1");
  harness.client.deliver(kSensorReadTopic, "1 0");
  harness.client.deliver(kSensorReadTopic, "1 17");
  harness.client.deliver("watering/pump", "1 1");
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(afterCount + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());
  SensorDemandResult demand;
  TEST_ASSERT_FALSE(harness.poller.takeDemandResult(&demand));
}

void testSensorReadCommandUnavailableWhenSensorMissing()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  const size_t afterCount = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorReadTopic, "1 2");
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCount, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/2",
                             "unavailable", false));
}

void testPeriodicSensorPublishEachReadingAndClearOnUnplug()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = false;
  harness.device.sensorValue[0] = 0;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "disconnected", true));

  harness.client.publishedMessages.clear();
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 2600;
  harness.clock.advance(60000);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2600", true));
  const size_t once = harness.client.publishedMessages.size();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once, harness.client.publishedMessages.size());

  harness.client.publishedMessages.clear();
  harness.clock.advance(60000);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2600", true));
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "unavailable", true));
}

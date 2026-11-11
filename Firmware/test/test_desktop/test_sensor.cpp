#include <unity.h>

#include <cstring>

#include "ModuleCodec.h"
#include "ModuleProtocol.h"
#include "SensorCommands.h"
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
 * Counts bus frames whose command byte matches.
 *
 * @param fixture Host fixture.
 * @param command Command byte.
 * @return Number of matching frames.
 */
int countCommand(const EmptyModuleHostFixture& fixture, uint8_t command)
{
  int count = 0;
  for (size_t i = 0; i < fixture.bus.ops.size(); ++i)
  {
    const FakeI2cOp& op = fixture.bus.ops[i];
    if (op.tx.size() > 1 && op.tx[1] == command)
    {
      ++count;
    }
  }
  return count;
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
  MqttTopicLayout topics;
  SensorMqttBridge bridge;

  /**
   * Creates an online-ready sensor harness with MQTT connected.
   */
  SensorHarness()
    : modules(clock),
      device(clock, modules.mod1),
      mqtt(client, clock, sensorMqttConfig()),
      poller(modules.host, clock),
      topics("watering"),
      bridge(poller, mqtt, topics)
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
 * Marks one input on so a state query is followed by a reading.
 *
 * @param device Simulated module.
 * @param index Zero-based input.
 * @param value Raw value the reading returns.
 * @return Nothing.
 */
void setInputOn(FakeModuleDevice& device, uint8_t index, int32_t value)
{
  device.sensorState[index] = module_protocol::kSensorStateOn;
  device.sensorConnected[index] = true;
  device.sensorValue[index] = value;
}

/**
 * Runs one state query for each input, and a reading when that
 * input is on. The count must already be known.
 *
 * @param harness Sensor harness.
 * @param count Inputs to walk.
 * @return Nothing.
 */
void readInputs(SensorHarness& harness, uint8_t count)
{
  for (uint8_t index = 0; index < count; ++index)
  {
    harness.poller.update();
    if (harness.device.sensorState[index] == module_protocol::kSensorStateOn)
    {
      harness.poller.update();
    }
  }
}

/**
 * Queries the count, then state and, when on, a reading for each input.
 *
 * @param harness Sensor harness.
 * @param count Inputs the device reports.
 * @return Nothing.
 */
void learnInputs(SensorHarness& harness, uint8_t count)
{
  harness.poller.update();
  readInputs(harness, count);
}

/**
 * Counts publishes of one topic.
 *
 * @param client Fake broker.
 * @param topic Expected topic.
 * @return Number of matching messages.
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
  const SensorCountResult count = querySensorCount(fixture.host, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, count.status);
  TEST_ASSERT_EQUAL(2, count.count);
  TEST_ASSERT_EQUAL(before + 1, fixture.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(0x10, fixture.bus.ops.back().address);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount, lastCommand(fixture));

  const SensorConnectedResult presence =
      querySensorConnected(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, presence.status);
  TEST_ASSERT_FALSE(presence.connected);
  TEST_ASSERT_EQUAL(1, lastSensorIndex(fixture));

  const SensorReadingResult reading = querySensorReading(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, reading.status);
  TEST_ASSERT_TRUE(reading.connected);
  TEST_ASSERT_EQUAL(-123456, reading.value);

  const SensorStateResult off = querySensorState(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, off.status);
  TEST_ASSERT_TRUE(off.state == SensorState::Off);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState, lastCommand(fixture));

  device.sensorConnected[0] = true;
  const size_t beforeSet = fixture.bus.protocolOpCount();
  const SensorStateResult turningOn = setSensor(fixture.host, 0, 0, true);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Busy, turningOn.status);
  TEST_ASSERT_EQUAL(beforeSet + 1, fixture.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(fixture));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(fixture));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOn,
                    fixture.bus.ops.back().tx[3]);

  const SensorStateResult settled = querySensorState(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, settled.status);
  TEST_ASSERT_TRUE(settled.state == SensorState::On);

  const SensorStateResult turnedOff = setSensor(fixture.host, 0, 0, false);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, turnedOff.status);
  TEST_ASSERT_TRUE(turnedOff.state == SensorState::Off);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(fixture));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOff,
                    fixture.bus.ops.back().tx[3]);

  device.sensorState[0] = module_protocol::kSensorStateFault;
  const SensorStateResult ignored = setSensor(fixture.host, 0, 0, true);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, ignored.status);
  TEST_ASSERT_TRUE(ignored.state == SensorState::Fault);

  const SensorStateResult reset = resetSensor(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, reset.status);
  TEST_ASSERT_TRUE(reset.state == SensorState::Off);
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetSensor, lastCommand(fixture));

  device.sensorState[1] = 9;
  const SensorStateResult badByte = querySensorState(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, badByte.status);

  const size_t beforeReject = fixture.bus.protocolOpCount();
  const SensorStateResult outOfRange = querySensorState(
      fixture.host, 0, module_protocol::kMaxSensorsPerModule);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, outOfRange.status);
  TEST_ASSERT_EQUAL(beforeReject, fixture.bus.protocolOpCount());

  device.forceBusy = true;
  const SensorReadingResult busy = querySensorReading(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Busy, busy.status);
}

void testHostRejectsSensorQueryUnlessOnlineSensor()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  const size_t before = fixture.bus.protocolOpCount();
  const SensorCountResult absent = querySensorCount(fixture.host, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, absent.status);
  TEST_ASSERT_EQUAL(before, fixture.bus.protocolOpCount());

  FakeModuleDevice device(clock, fixture.mod1);
  plugAndPump(fixture, 0, device);
  const size_t online = fixture.bus.protocolOpCount();
  const SensorReadingResult echo = querySensorReading(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, echo.status);
  TEST_ASSERT_EQUAL(online, fixture.bus.protocolOpCount());

  const SensorConnectedResult outOfRange =
      querySensorConnected(fixture.host, 0, module_protocol::kMaxSensorsPerModule);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, outOfRange.status);
  TEST_ASSERT_EQUAL(online, fixture.bus.protocolOpCount());

  const SensorFaultResult fault = querySensorFault(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Rejected, fault.status);
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
  const SensorCountResult count = querySensorCount(fixture.host, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, count.status);
  const SensorReadingResult reading = querySensorReading(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, reading.status);
  const SensorStateResult state = querySensorState(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, state.status);
  const SensorStateResult written = setSensor(fixture.host, 0, 0, true);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, written.status);
  const SensorStateResult reset = resetSensor(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, reset.status);
  const SensorFaultResult fault = querySensorFault(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, fault.status);
}

void testPollerQueriesCountThenEachSensorAtInterval()
{
  SensorHarness harness;
  harness.device.sensorCount = 2;
  setInputOn(harness.device, 0, 10);
  harness.device.sensorState[1] = module_protocol::kSensorStateDisconnected;
  harness.device.sensorConnected[1] = false;
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
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
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
  TEST_ASSERT_FALSE(hasValue);

  const size_t afterCycle = harness.modules.bus.protocolOpCount();
  harness.clock.advance(59999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle, harness.modules.bus.protocolOpCount());
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
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
  setInputOn(harness.device, 0, 1);
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.device.forceBusy = true;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    harness.poller.update();
    TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                      lastCommand(harness.modules));
  }
  const size_t afterSkip = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterSkip, harness.modules.bus.protocolOpCount());
  harness.device.forceBusy = false;
  harness.clock.advance(60000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
}

void testSensorReadCommandPublishesWithoutBusUntilUpdate()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 2500);
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  const size_t afterCount = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kSensorReadTopic, "1 1");
  TEST_ASSERT_EQUAL(afterCount, harness.modules.bus.protocolOpCount());

  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCount + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", false));
}

void testSensorReadCommandRepeatsUnchangedReading()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 2500);
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  harness.client.publishedMessages.clear();

  harness.client.deliver(kSensorReadTopic, "1 1");
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensor/1"));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", false));
}

void testSensorReadCommandRejectsMalformedPayload()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 3);
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
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "1 1", false));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "connected 3", false));
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
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/2",
                              "", true));
}

void testPeriodicSensorPublishEachReadingAndClearOnUnplug()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateDisconnected;
  harness.device.sensorConnected[0] = false;
  harness.device.sensorValue[0] = 0;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "disconnected", false));

  harness.client.publishedMessages.clear();
  setInputOn(harness.device, 0, 2600);
  harness.clock.advance(60000);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2600", false));
  const int once = countTopic(harness.client, "watering/slot/1/sensor/1");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once, countTopic(harness.client, "watering/slot/1/sensor/1"));

  harness.client.publishedMessages.clear();
  harness.clock.advance(60000);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensor/1"));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2600", false));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "", true));
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensor/1"));

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "unavailable", false));
}

void testSensorBridgeFollowsDeviceId()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 2500);
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "1 1", false));
  harness.client.publishedMessages.clear();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client, "watering/slot/1/sensor/1"));
  TEST_ASSERT_EQUAL(0, countTopic(harness.client, "watering/slot/1/sensors"));

  harness.client.publishedMessages.clear();
  harness.topics.setDeviceId("shed");
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorReadTopic, "1 1");
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "shed/slot/1/sensor/1",
                             "connected 2500", false));
  TEST_ASSERT_TRUE(published(harness.client, "shed/slot/1/sensors",
                             "1 1", false));
  TEST_ASSERT_FALSE(published(harness.client, "shed/slot/1/sensor/1",
                              "", true));

  harness.client.publishedMessages.clear();
  harness.client.deliver(harness.topics.sensorRead(), "1 1");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "shed/slot/1/sensor/1",
                             "connected 2500", false));
}

void testSensorConnectedListPublishesIndexes()
{
  SensorHarness harness;
  harness.device.sensorCount = 4;
  setInputOn(harness.device, 0, 1);
  setInputOn(harness.device, 1, 2);
  harness.device.sensorState[2] = module_protocol::kSensorStateDisconnected;
  setInputOn(harness.device, 3, 4);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 4);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "4 1 2 4", false));

  harness.client.publishedMessages.clear();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client, "watering/slot/1/sensors"));

  harness.clock.advance(60000);
  readInputs(harness, 4);
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client, "watering/slot/1/sensors"));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 1", false));

  harness.client.publishedMessages.clear();
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorConnectedTopic, "1");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "4 1 2 4", false));
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
}

void testSensorConnectedQueryWaitsUntilPresenceIsKnown()
{
  SensorHarness harness;
  harness.device.sensorCount = 2;
  setInputOn(harness.device, 0, 10);
  harness.device.sensorState[1] = module_protocol::kSensorStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);
  const size_t seated = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorConnectedTopic, "1");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(seated, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensors",
                              "2 1", false));

  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensors",
                              "2 1", false));

  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensors",
                              "2 1", false));

  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensors",
                              "2 1", false));

  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastSensorIndex(harness.modules));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "2 1", false));
}

void testSensorConnectedQueryRejectsMalformedPayload()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 3);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.bridge.update();
  harness.client.publishedMessages.clear();
  const size_t learned = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kSensorConnectedTopic, "1 2");
  harness.client.deliver(kSensorConnectedTopic, "5");
  harness.client.deliver(kSensorConnectedTopic, "on");
  harness.client.deliver(kSensorConnectedTopic, "");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client, "watering/slot/1/sensors"));
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
}

void testSensorConnectedListForEmptyAndAllAbsent()
{
  SensorHarness empty;
  empty.device.sensorCount = 0;
  plugAndPump(empty.modules, 0, empty.device);
  empty.poller.update();
  empty.bridge.update();
  TEST_ASSERT_TRUE(published(empty.client, "watering/slot/1/sensors",
                             "0", false));
  empty.client.publishedMessages.clear();
  empty.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(empty.client, "watering/slot/1/sensors"));

  SensorHarness absent;
  absent.device.sensorCount = 4;
  absent.device.sensorState[0] = module_protocol::kSensorStateDisconnected;
  absent.device.sensorState[1] = module_protocol::kSensorStateDisconnected;
  absent.device.sensorState[2] = module_protocol::kSensorStateDisconnected;
  absent.device.sensorState[3] = module_protocol::kSensorStateDisconnected;
  plugAndPump(absent.modules, 0, absent.device);
  learnInputs(absent, 4);
  absent.bridge.update();
  TEST_ASSERT_TRUE(published(absent.client, "watering/slot/1/sensors",
                             "4", false));
}

void testSensorConnectedListFollowsPresenceChange()
{
  SensorHarness harness;
  harness.device.sensorCount = 2;
  setInputOn(harness.device, 0, 10);
  harness.device.sensorState[1] = module_protocol::kSensorStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 2);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "2 1", false));

  setInputOn(harness.device, 1, 7);
  harness.clock.advance(60000);
  harness.client.publishedMessages.clear();
  readInputs(harness, 2);
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensors"));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "2 1 2", false));
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensors"));
}

void testSensorConnectedInventoryClearsWhenModuleUnplugged()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 4);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "1 1", false));

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "unavailable", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "unavailable", false));
}

void testBootPublishesOffAndDoesNotRead()
{
  SensorHarness harness;
  harness.device.sensorCount = 3;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  for (uint8_t index = 0; index < 3; ++index)
  {
    harness.poller.update();
    TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                      lastCommand(harness.modules));
    TEST_ASSERT_EQUAL(index, lastSensorIndex(harness.modules));
  }
  const size_t afterCycle = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "off", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/2",
                             "off", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/3",
                             "off", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "3", false));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "disconnected", false));
}

void testSensorOnConfirmsStateThenPublishesReading()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 2500;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "off", false));
  harness.client.publishedMessages.clear();

  const size_t before = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  TEST_ASSERT_EQUAL(before, harness.modules.bus.protocolOpCount());

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOn,
                    harness.modules.bus.ops.back().tx[3]);
  harness.bridge.update();
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "unavailable", false));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "unavailable", false));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "connected 2500", false));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 2500", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "1 1", false));
}

void testSensorOnWithOpenJackPublishesDisconnected()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = false;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  const size_t afterConfirm = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterConfirm, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "disconnected", false));
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "connected 0", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "1", false));
}

void testSensorOffConfirmsAndSecondOffSkipsSet()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 80);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.client.deliver(kSensorCommandTopic, "1 1 off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOff,
                    harness.modules.bus.ops.back().tx[3]);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "off", false));

  harness.client.deliver(kSensorCommandTopic, "1 1 off");
  const size_t before = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(before + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
}

void testSensorOnWhileAlreadyOnConfirmsWithoutSet()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 80);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.client.publishedMessages.clear();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 80", false));
}

void testSensorFaultIgnoresOnAndOffUntilReset()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "fault", false));

  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  const size_t beforeOn = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(beforeOn + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));

  harness.client.deliver(kSensorCommandTopic, "1 1 off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));

  harness.client.deliver(kSensorCommandTopic, "1 1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetSensor,
                    lastCommand(harness.modules));
  SensorState state = SensorState::Fault;
  TEST_ASSERT_TRUE(harness.poller.sensorState(0, 0, &state));
  TEST_ASSERT_TRUE(state == SensorState::Off);
  const size_t afterReset = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterReset, harness.modules.bus.protocolOpCount());

  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  harness.client.deliver(kSensorCommandTopic, "1 1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetSensor,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOn,
                    harness.modules.bus.ops.back().tx[3]);
}

void testPeriodicFaultIsNotReset()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 12);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  harness.clock.advance(60000);
  const size_t before = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(before + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "fault", false));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "1 overcurrent", false));
  const size_t after = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(after, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateFault,
                    harness.device.sensorState[0]);
}

void testImmediateReadOfOffPublishesOff()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  harness.client.publishedMessages.clear();
  harness.client.deliver(kSensorReadTopic, "1 1");
  const size_t before = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(before, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, countTopic(harness.client, "watering/slot/1/sensor/1"));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "off", false));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOff,
                    harness.device.sensorState[0]);
}

void testSensorCommandRejectsMalformedPayload()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSensorCommandTopic, "1 1 maybe");
  harness.client.deliver(kSensorCommandTopic, "on");
  harness.client.deliver(kSensorCommandTopic, "1 on");
  harness.client.deliver(kSensorCommandTopic, "1 1 ON");
  harness.client.deliver(kSensorCommandTopic, "1 1 on extra");
  harness.client.deliver(kSensorCommandTopic, "1 17 on");
  harness.client.deliver(kSensorCommandTopic, "0 1 off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  harness.bridge.update();
  TEST_ASSERT_FALSE(published(harness.client, "watering/slot/1/sensor/1",
                              "connected 0", false));
}

void testFifthSensorReadIsDropped()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  harness.client.publishedMessages.clear();
  for (int i = 0; i < 5; ++i)
  {
    harness.client.deliver(kSensorReadTopic, "1 1");
  }
  for (int i = 0; i < 4; ++i)
  {
    harness.poller.update();
    harness.bridge.update();
  }
  TEST_ASSERT_EQUAL(4, countTopic(harness.client, "watering/slot/1/sensor/1"));
  const size_t after = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(after, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(4, countTopic(harness.client, "watering/slot/1/sensor/1"));
}

void testSensorCommandKeepsDesiredAcrossNewIdentity()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorConnected[0] = true;
  harness.device.sensorValue[0] = 1800;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(harness.modules));

  harness.device.sensorState[0] = module_protocol::kSensorStateOff;
  harness.device.sensorAwaitingSample[0] = false;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSensorStateOn,
                    harness.modules.bus.ops.back().tx[3]);
}

void testSensorCommandDropsDesiredWhenSlotChangesType()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");

  harness.device.typeId = module_protocol::kTypeSolenoidModule;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  harness.poller.update();

  harness.device.typeId = module_protocol::kTypeSensorModule;
  harness.device.sensorState[0] = module_protocol::kSensorStateOff;
  harness.device.sensorAwaitingSample[0] = false;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  TEST_ASSERT_EQUAL(module_protocol::kTypeSensorModule,
                    harness.modules.host.typeId(0));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  const size_t afterState = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterState, harness.modules.bus.protocolOpCount());
}

void testConnectedListNamesOnlyInputsThatAreOn()
{
  SensorHarness harness;
  harness.device.sensorCount = 3;
  setInputOn(harness.device, 0, 5);
  harness.device.sensorState[1] = module_protocol::kSensorStateOff;
  harness.device.sensorState[2] = module_protocol::kSensorStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 3);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors",
                             "3 1", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/1",
                             "connected 5", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/2",
                             "off", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/3",
                             "disconnected", false));
  const int once = countTopic(harness.client, "watering/slot/1/sensors");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once, countTopic(harness.client, "watering/slot/1/sensors"));
  harness.client.deliver(kSensorConnectedTopic, "1");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once + 1,
                    countTopic(harness.client, "watering/slot/1/sensors"));
}

void testHostReadsSensorFaultList()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 3;
  device.sensorState[1] = module_protocol::kSensorStateFault;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);

  const SensorFaultResult list = querySensorFault(fixture.host, 0, 3);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, list.status);
  TEST_ASSERT_EQUAL(3, list.report.count);
  TEST_ASSERT_EQUAL(module_protocol::kFaultNone, list.report.reason[0]);
  TEST_ASSERT_EQUAL(module_protocol::kSensorFaultOvercurrent,
                    list.report.reason[1]);
  TEST_ASSERT_EQUAL(module_protocol::kFaultNone, list.report.reason[2]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault, lastCommand(fixture));
  TEST_ASSERT_EQUAL(3, fixture.bus.ops.back().tx.size());

  device.sensorCount = 0;
  const SensorFaultResult empty = querySensorFault(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Ok, empty.status);
  TEST_ASSERT_EQUAL(0, empty.report.count);
  const SensorFaultResult emptyRejected = querySensorFault(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, emptyRejected.status);

  device.sensorCount = 3;
  device.sensorFaultRaw = true;
  device.sensorFaultRawLen = 6;
  device.sensorFaultRawBody[0] = module_protocol::kFaultNone;
  device.sensorFaultRawBody[1] = 0;
  device.sensorFaultRawBody[2] = module_protocol::kSensorFaultOvercurrent;
  device.sensorFaultRawBody[3] = 1;
  device.sensorFaultRawBody[4] = module_protocol::kFaultNone;
  device.sensorFaultRawBody[5] = 2;
  const SensorFaultResult notEmpty = querySensorFault(fixture.host, 0, 0);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, notEmpty.status);

  device.sensorFaultRawLen = 1;
  device.sensorFaultRawBody[0] = 0;
  const SensorFaultResult odd = querySensorFault(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, odd.status);

  device.sensorFaultRawLen = 2;
  device.sensorFaultRawBody[0] = module_protocol::kFaultNone;
  device.sensorFaultRawBody[1] = 0;
  const SensorFaultResult shortList = querySensorFault(fixture.host, 0, 3);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, shortList.status);

  device.sensorFaultRawBody[1] = 1;
  const SensorFaultResult badIndex = querySensorFault(fixture.host, 0, 1);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, badIndex.status);

  device.sensorFaultRaw = false;
  device.sensorCount = 9;
  device.sensorState[0] = module_protocol::kSensorStateFault;
  const SensorFaultResult unsupported = querySensorFault(fixture.host, 0, 9);
  TEST_ASSERT_EQUAL(SensorQueryStatus::Failed, unsupported.status);
}

void testSensorOnAndOffDoNotReadFault()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  setInputOn(harness.device, 0, 80);
  plugAndPump(harness.modules, 0, harness.device);
  learnInputs(harness, 1);
  harness.client.deliver(kSensorCommandTopic, "1 1 off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSensor,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  const size_t after = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(after, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(0, countCommand(harness.modules,
                                    module_protocol::kCmdGetFault));
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client,
                                  "watering/slot/1/sensors/fault"));
}

void testSensorFaultListPublishesReasons()
{
  SensorHarness harness;
  harness.device.sensorCount = 3;
  harness.device.sensorState[1] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensor/2",
                             "fault", false));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 none overcurrent none", false));
  const int once = countTopic(harness.client, "watering/slot/1/sensors/fault");
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once,
                    countTopic(harness.client, "watering/slot/1/sensors/fault"));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(2, lastSensorIndex(harness.modules));

  harness.device.sensorState[2] = module_protocol::kSensorStateFault;
  harness.clock.advance(60000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(1, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(2, lastSensorIndex(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 none overcurrent overcurrent", false));
}

void testSensorFaultClearPublishesNoneWithoutRead()
{
  SensorHarness harness;
  harness.device.sensorCount = 3;
  harness.device.sensorState[1] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(2, lastSensorIndex(harness.modules));

  harness.client.deliver(kSensorCommandTopic, "1 2 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetSensor,
                    lastCommand(harness.modules));
  const size_t afterReset = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterReset, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(1, countCommand(harness.modules,
                                    module_protocol::kCmdGetFault));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 none none none", false));
}

void testSensorFaultReadWaitsAfterThreeBusyReplies()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.device.sensorFaultBusyReads = 3;
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  const size_t waiting = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(waiting, harness.modules.bus.protocolOpCount());
  harness.clock.advance(60000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "1 overcurrent", false));
}

void testSensorFaultReadRunsBeforeConfirm()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.client.deliver(kSensorCommandTopic, "1 1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorState,
                    lastCommand(harness.modules));
}

void testSensorFaultReadRunsBeforeReset()
{
  SensorHarness harness;
  harness.device.sensorCount = 3;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 overcurrent none none", false));

  harness.device.sensorState[2] = module_protocol::kSensorStateFault;
  harness.clock.advance(60000);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  harness.client.deliver(kSensorCommandTopic, "1 1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 overcurrent none overcurrent", false));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetSensor,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastSensorIndex(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "3 none none overcurrent", false));
  TEST_ASSERT_EQUAL(2, countCommand(harness.modules,
                                    module_protocol::kCmdGetFault));
}

void testSensorFaultBadListIsNotStored()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  harness.device.sensorFaultRaw = true;
  harness.device.sensorFaultRawLen = 1;
  harness.device.sensorFaultRawBody[0] = 0;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  TEST_ASSERT_FALSE(harness.poller.faultReportKnown(0));
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, countTopic(harness.client,
                                  "watering/slot/1/sensors/fault"));
}

void testSensorFaultUnsupportedIsNotStored()
{
  SensorHarness harness;
  harness.device.sensorCount = 9;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  TEST_ASSERT_FALSE(harness.poller.faultReportKnown(0));
}

void testSensorFaultDecimalReasonAndUnplug()
{
  SensorHarness harness;
  harness.device.sensorCount = 1;
  harness.device.sensorState[0] = module_protocol::kSensorStateFault;
  harness.device.sensorFaultReasonSet[0] = true;
  harness.device.sensorFaultReason[0] = 9;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.poller.update();
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetFault,
                    lastCommand(harness.modules));
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "1 9", false));

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/sensors/fault",
                             "unavailable", false));
}

// ========== Runner ==========

/**
 * Registers the sensor command, poller, and bridge cases.
 *
 * @return Nothing.
 */
void runSensorTests()
{
  // UNITY_BEGIN records test_main.cpp, so name this file for the report.
  UnitySetTestFile(__FILE__);
  RUN_TEST(testCodecEncodesSensorCommands);
  RUN_TEST(testHostReadsSensorCountConnectedAndReading);
  RUN_TEST(testHostRejectsSensorQueryUnlessOnlineSensor);
  RUN_TEST(testHostRejectsShortSensorPayload);
  RUN_TEST(testHostReadsSensorFaultList);
  RUN_TEST(testPollerQueriesCountThenEachSensorAtInterval);
  RUN_TEST(testPollerRequeriesCountAfterModuleReset);
  RUN_TEST(testPollerIdleWhenSensorCountIsZero);
  RUN_TEST(testPollerRetriesBusyThenMovesOn);
  RUN_TEST(testSensorReadCommandPublishesWithoutBusUntilUpdate);
  RUN_TEST(testSensorReadCommandRepeatsUnchangedReading);
  RUN_TEST(testSensorReadCommandRejectsMalformedPayload);
  RUN_TEST(testSensorReadCommandUnavailableWhenSensorMissing);
  RUN_TEST(testPeriodicSensorPublishEachReadingAndClearOnUnplug);
  RUN_TEST(testSensorBridgeFollowsDeviceId);
  RUN_TEST(testSensorConnectedListPublishesIndexes);
  RUN_TEST(testSensorConnectedQueryWaitsUntilPresenceIsKnown);
  RUN_TEST(testSensorConnectedQueryRejectsMalformedPayload);
  RUN_TEST(testSensorConnectedListForEmptyAndAllAbsent);
  RUN_TEST(testSensorConnectedListFollowsPresenceChange);
  RUN_TEST(testSensorConnectedInventoryClearsWhenModuleUnplugged);
  RUN_TEST(testBootPublishesOffAndDoesNotRead);
  RUN_TEST(testSensorOnConfirmsStateThenPublishesReading);
  RUN_TEST(testSensorOnWithOpenJackPublishesDisconnected);
  RUN_TEST(testSensorOffConfirmsAndSecondOffSkipsSet);
  RUN_TEST(testSensorOnWhileAlreadyOnConfirmsWithoutSet);
  RUN_TEST(testSensorFaultIgnoresOnAndOffUntilReset);
  RUN_TEST(testPeriodicFaultIsNotReset);
  RUN_TEST(testImmediateReadOfOffPublishesOff);
  RUN_TEST(testSensorCommandRejectsMalformedPayload);
  RUN_TEST(testFifthSensorReadIsDropped);
  RUN_TEST(testSensorCommandKeepsDesiredAcrossNewIdentity);
  RUN_TEST(testSensorCommandDropsDesiredWhenSlotChangesType);
  RUN_TEST(testConnectedListNamesOnlyInputsThatAreOn);
  RUN_TEST(testSensorOnAndOffDoNotReadFault);
  RUN_TEST(testSensorFaultListPublishesReasons);
  RUN_TEST(testSensorFaultClearPublishesNoneWithoutRead);
  RUN_TEST(testSensorFaultReadWaitsAfterThreeBusyReplies);
  RUN_TEST(testSensorFaultReadRunsBeforeConfirm);
  RUN_TEST(testSensorFaultReadRunsBeforeReset);
  RUN_TEST(testSensorFaultBadListIsNotStored);
  RUN_TEST(testSensorFaultUnsupportedIsNotStored);
  RUN_TEST(testSensorFaultDecimalReasonAndUnplug);
}

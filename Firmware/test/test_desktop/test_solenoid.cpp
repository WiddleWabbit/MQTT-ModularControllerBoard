#include <unity.h>

#include <cstring>

#include "ModuleCodec.h"
#include "ModuleProtocol.h"
#include "SensorMqttBridge.h"
#include "SolenoidMqttBridge.h"
#include "SolenoidPoller.h"
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
 * Reads one byte of the latest request frame.
 *
 * @param fixture Host fixture.
 * @param index Frame index.
 * @return Byte, or 0xFF when the frame is shorter.
 */
uint8_t lastTx(const EmptyModuleHostFixture& fixture, size_t index)
{
  const FakeI2cOp& op = fixture.bus.ops.back();
  return index < op.tx.size() ? op.tx[index] : 0xFF;
}

/**
 * Returns a broker config subscribed to the solenoid command.
 *
 * @return MQTT service configuration.
 */
MqttConfig solenoidMqttConfig()
{
  static const MqttSubscription subscriptions[] = {
    {kSolenoidCommandTopic, 1},
  };
  return {"watering-controller", nullptr, nullptr, subscriptions, 1, 100, 400};
}

/**
 * Solenoid module, poller, and MQTT bridge wired the way loop() calls them.
 */
struct SolenoidHarness
{
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeModuleDevice device;
  FakeMqttClient client;
  MqttService mqtt;
  SolenoidPoller poller;
  SolenoidMqttBridge bridge;

  /**
   * Creates an online-ready solenoid harness with MQTT connected.
   *
   * @param pollIntervalMs State-poll period.
   * @param commandTimeoutMs Silence before every output is turned off.
   */
  explicit SolenoidHarness(uint32_t pollIntervalMs = 60000,
                           uint32_t commandTimeoutMs = 15UL * 60UL * 1000UL)
    : modules(clock),
      device(clock, modules.mod1),
      mqtt(client, clock, solenoidMqttConfig()),
      poller(modules.host, clock, pollIntervalMs, commandTimeoutMs),
      bridge(poller, mqtt, "watering/slot")
  {
    device.typeId = module_protocol::kTypeSolenoidModule;
    modules.host.begin();
    mqtt.setBroker("broker.local", 1883);
    mqtt.begin();
    mqtt.update(true);
    mqtt.setMessageHandler(SolenoidMqttBridge::onMqttMessage, &bridge);
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

/**
 * Reads the output count and then each output once.
 *
 * @param harness Solenoid harness.
 * @param count Outputs the fake module reports.
 * @return Nothing.
 */
void learnOutputs(SolenoidHarness& harness, uint8_t count)
{
  harness.poller.update();
  for (uint8_t index = 0; index < count; ++index)
  {
    harness.poller.update();
  }
}
}

void testCodecEncodesSolenoidCommands()
{
  uint8_t tx[8] = {};
  const size_t countSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdGetSolenoidCount, nullptr, 0, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(3, countSize);
  TEST_ASSERT_EQUAL(2, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 2), tx[2]);

  const uint8_t index = 3;
  const size_t stateSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdGetSolenoidState, &index, 1, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(4, stateSize);
  TEST_ASSERT_EQUAL(3, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState, tx[1]);
  TEST_ASSERT_EQUAL(3, tx[2]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 3), tx[3]);

  const uint8_t setPayload[2] = {1, module_protocol::kSolenoidStateOn};
  const size_t setSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdSetSolenoid, setPayload, 2, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(5, setSize);
  TEST_ASSERT_EQUAL(4, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, tx[1]);
  TEST_ASSERT_EQUAL(1, tx[2]);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, tx[3]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 4), tx[4]);
}

void testHostReadsSolenoidCountStateAndSet()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypeSolenoidModule;
  device.solenoidCount = 3;
  device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  device.solenoidState[1] = module_protocol::kSolenoidStateOn;
  device.solenoidState[2] = module_protocol::kSolenoidStateDisconnected;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL_STRING("Solenoid", fixture.host.typeName(0));

  const SolenoidCountResult count = fixture.host.querySolenoidCount(0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Ok, count.status);
  TEST_ASSERT_EQUAL(3, count.count);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount, lastCommand(fixture));

  const SolenoidStateResult off = fixture.host.querySolenoidState(0, 0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Ok, off.status);
  TEST_ASSERT_EQUAL(SolenoidOutputState::Off, off.state);
  const SolenoidStateResult on = fixture.host.querySolenoidState(0, 1);
  TEST_ASSERT_EQUAL(SolenoidOutputState::On, on.state);
  const SolenoidStateResult open = fixture.host.querySolenoidState(0, 2);
  TEST_ASSERT_EQUAL(SolenoidOutputState::Disconnected, open.state);

  const SolenoidStateResult turned = fixture.host.setSolenoid(0, 0, true);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Ok, turned.status);
  TEST_ASSERT_EQUAL(SolenoidOutputState::On, turned.state);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(fixture));
  TEST_ASSERT_EQUAL(0, lastTx(fixture, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, lastTx(fixture, 3));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, device.solenoidState[0]);

  const SolenoidStateResult stillOpen = fixture.host.setSolenoid(0, 2, true);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Ok, stillOpen.status);
  TEST_ASSERT_EQUAL(SolenoidOutputState::Disconnected, stillOpen.state);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateDisconnected,
                    device.solenoidState[2]);

  device.solenoidState[1] = 9;
  const SolenoidStateResult bad = fixture.host.querySolenoidState(0, 1);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Failed, bad.status);

  device.forceBusy = true;
  const SolenoidStateResult busy = fixture.host.querySolenoidState(0, 0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Busy, busy.status);
}

void testHostRejectsSolenoidQueryUnlessOnlineSolenoid()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  const size_t before = fixture.bus.protocolOpCount();
  const SolenoidCountResult absent = fixture.host.querySolenoidCount(0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Rejected, absent.status);
  TEST_ASSERT_EQUAL(before, fixture.bus.protocolOpCount());

  FakeModuleDevice device(clock, fixture.mod1);
  plugAndPump(fixture, 0, device);
  const size_t online = fixture.bus.protocolOpCount();
  const SolenoidStateResult echo = fixture.host.querySolenoidState(0, 0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Rejected, echo.status);
  const SolenoidStateResult set = fixture.host.setSolenoid(0, 0, true);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Rejected, set.status);
  TEST_ASSERT_EQUAL(online, fixture.bus.protocolOpCount());

  device.typeId = module_protocol::kTypeSensorModule;
  device.resetToUnconfigured();
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(module_protocol::kTypeSensorModule, fixture.host.typeId(0));
  const size_t sensor = fixture.bus.protocolOpCount();
  const SolenoidCountResult wrong = fixture.host.querySolenoidCount(0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Rejected, wrong.status);
  TEST_ASSERT_EQUAL(sensor, fixture.bus.protocolOpCount());

  const SolenoidStateResult outOfRange = fixture.host.querySolenoidState(
      0, module_protocol::kMaxSolenoidsPerModule);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Rejected, outOfRange.status);
}

void testHostRejectsShortSolenoidPayload()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypeSolenoidModule;
  device.solenoidCount = 1;
  device.shortSolenoidPayload = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  const SolenoidCountResult count = fixture.host.querySolenoidCount(0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Failed, count.status);
  const SolenoidStateResult state = fixture.host.querySolenoidState(0, 0);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Failed, state.status);
  const SolenoidStateResult set = fixture.host.setSolenoid(0, 0, false);
  TEST_ASSERT_EQUAL(SolenoidQueryStatus::Failed, set.status);
}

void testSolenoidTimingDefaultsAreFifteenMinutesAndOneMinute()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  SolenoidPoller poller(fixture.host, clock);
  TEST_ASSERT_EQUAL(60000, poller.pollIntervalMs());
  TEST_ASSERT_EQUAL(15UL * 60UL * 1000UL, poller.commandTimeoutMs());
  SolenoidPoller zeros(fixture.host, clock, 0, 0);
  TEST_ASSERT_EQUAL(60000, zeros.pollIntervalMs());
  TEST_ASSERT_EQUAL(15UL * 60UL * 1000UL, zeros.commandTimeoutMs());
}

void testPollerQueriesCountThenEachSolenoidAtInterval()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 2;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOn;
  harness.device.solenoidState[1] = module_protocol::kSolenoidStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(2, harness.poller.solenoidCount(0));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  SolenoidOutputState state = SolenoidOutputState::Off;
  TEST_ASSERT_TRUE(harness.poller.solenoidState(0, 0, &state));
  TEST_ASSERT_EQUAL(SolenoidOutputState::On, state);

  harness.poller.update();
  TEST_ASSERT_EQUAL(1, lastTx(harness.modules, 2));
  TEST_ASSERT_TRUE(harness.poller.solenoidState(0, 1, &state));
  TEST_ASSERT_EQUAL(SolenoidOutputState::Disconnected, state);

  const size_t afterCycle = harness.modules.bus.protocolOpCount();
  harness.clock.advance(59999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle, harness.modules.bus.protocolOpCount());
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCycle + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
}

void testPollerRequeriesSolenoidCountAfterModuleReset()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 2;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_EQUAL(2, harness.poller.solenoidCount(0));
  const uint32_t firstEpoch = harness.modules.host.identityEpoch(0);

  harness.device.solenoidCount = 4;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(harness.modules.host.identityEpoch(0) > firstEpoch);

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(4, harness.poller.solenoidCount(0));
}

void testPollerIdleWhenSolenoidCountIsZero()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 0;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_TRUE(harness.poller.countKnown(0));
  TEST_ASSERT_EQUAL(0, harness.poller.solenoidCount(0));
  const size_t afterCount = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  harness.clock.advance(120000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterCount, harness.modules.bus.protocolOpCount());
}

void testPollerRetriesBusySolenoidStateThenMovesOn()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 1;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  harness.device.forceBusy = true;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    harness.poller.update();
    TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                      lastCommand(harness.modules));
  }
  const size_t afterGiveUp = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterGiveUp, harness.modules.bus.protocolOpCount());
}

void testSolenoidCommandSetsOnlyMismatchedOutputs()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 4;
  for (uint8_t index = 0; index < 4; ++index)
  {
    harness.device.solenoidState[index] = module_protocol::kSolenoidStateOff;
  }
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 4);
  const size_t learned = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kSolenoidCommandTopic, "1 on on off off");
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());

  harness.poller.update();
  TEST_ASSERT_EQUAL(learned + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, lastTx(harness.modules, 3));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, lastTx(harness.modules, 3));

  const size_t afterSets = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterSets, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, harness.device.solenoidState[0]);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, harness.device.solenoidState[1]);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, harness.device.solenoidState[2]);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, harness.device.solenoidState[3]);

  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/1",
                             "on", true));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/2",
                             "on", true));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/3",
                             "off", true));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/4",
                             "off", true));
}

void testSolenoidCommandSkipsDisconnectedAndTurnsOffEnergized()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 4;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOn;
  harness.device.solenoidState[1] = module_protocol::kSolenoidStateOff;
  harness.device.solenoidState[2] = module_protocol::kSolenoidStateOn;
  harness.device.solenoidState[3] = module_protocol::kSolenoidStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 4);

  harness.client.deliver(kSolenoidCommandTopic, "1 off off off off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, lastTx(harness.modules, 3));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(2, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, lastTx(harness.modules, 3));

  const size_t afterSets = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterSets, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateDisconnected,
                    harness.device.solenoidState[3]);
}

void testSolenoidCommandBeforeCountReadsStateThenSets()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 2;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  harness.device.solenoidState[1] = module_protocol::kSolenoidStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  const size_t plugged = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kSolenoidCommandTopic, "1 on off");
  TEST_ASSERT_EQUAL(plugged, harness.modules.bus.protocolOpCount());

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(1, lastTx(harness.modules, 2));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn,
                    harness.device.solenoidState[0]);
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff,
                    harness.device.solenoidState[1]);
}

void testSolenoidCommandRejectsMalformedPayload()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 2;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 2);
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kSolenoidCommandTopic, "1");
  harness.client.deliver(kSolenoidCommandTopic, "on on");
  harness.client.deliver(kSolenoidCommandTopic, "1 ON off");
  harness.client.deliver(kSolenoidCommandTopic, "0 off off");
  harness.client.deliver(kSolenoidCommandTopic, "5 off off");
  harness.client.deliver(kSolenoidCommandTopic, "1 off");
  harness.client.deliver(kSolenoidCommandTopic, "1 on on off off");
  harness.client.deliver("watering/pump", "1 on off");
  harness.client.deliver(kSensorReadTopic, "1 1");
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
}

void testSolenoidCommandReappliesAfterModuleReset()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 1;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 1);
  harness.client.deliver(kSolenoidCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, harness.device.solenoidState[0]);

  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, lastTx(harness.modules, 3));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, harness.device.solenoidState[0]);
}

void testSolenoidCommandTimeoutTurnsOutputsOff()
{
  SolenoidHarness harness(60000, 5000);
  harness.device.solenoidCount = 1;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 1);
  const size_t learned = harness.modules.bus.protocolOpCount();

  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());

  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(0, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, lastTx(harness.modules, 3));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, harness.device.solenoidState[0]);

  const size_t afterOff = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterOff, harness.modules.bus.protocolOpCount());
}

void testSolenoidCommandRefreshesAbsenceTimeout()
{
  SolenoidHarness harness(60000, 5000);
  harness.device.solenoidCount = 1;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 1);

  harness.client.deliver(kSolenoidCommandTopic, "1 on");
  harness.poller.update();
  const size_t afterMatch = harness.modules.bus.protocolOpCount();
  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterMatch, harness.modules.bus.protocolOpCount());

  harness.client.deliver(kSolenoidCommandTopic, "1 on");
  harness.poller.update();
  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterMatch, harness.modules.bus.protocolOpCount());

  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, lastTx(harness.modules, 3));
}

void testPeriodicSolenoidPublishEachStateAndClearOnUnplug()
{
  SolenoidHarness harness;
  harness.device.solenoidCount = 1;
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateDisconnected;
  plugAndPump(harness.modules, 0, harness.device);
  learnOutputs(harness, 1);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/1",
                             "disconnected", true));

  harness.client.publishedMessages.clear();
  harness.device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  harness.clock.advance(60000);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/1",
                             "off", true));
  const size_t once = harness.client.publishedMessages.size();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once, harness.client.publishedMessages.size());

  harness.client.publishedMessages.clear();
  harness.clock.advance(60000);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/1",
                             "off", true));

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/solenoid/1",
                             "unavailable", true));
}

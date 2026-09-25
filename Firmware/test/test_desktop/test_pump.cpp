#include <unity.h>

#include <cstring>

#include "ModuleCodec.h"
#include "ModuleProtocol.h"
#include "PumpMqttBridge.h"
#include "PumpPoller.h"
#include "SensorMqttBridge.h"
#include "SolenoidMqttBridge.h"
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
 * Returns a broker config subscribed to the pump command.
 *
 * @return MQTT service configuration.
 */
MqttConfig pumpMqttConfig()
{
  static const MqttSubscription subscriptions[] = {
    {kPumpCommandTopic, 1},
  };
  return {"watering-controller", nullptr, nullptr, subscriptions, 1, 100, 400};
}

/**
 * Pump module, poller, and MQTT bridge wired the way loop() calls them.
 */
struct PumpHarness
{
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeModuleDevice device;
  FakeMqttClient client;
  MqttService mqtt;
  PumpPoller poller;
  PumpMqttBridge bridge;

  /**
   * Creates an online-ready pump harness with MQTT connected.
   *
   * @param pollIntervalMs State-poll period.
   * @param commandTimeoutMs Silence before a running pump is turned off.
   */
  explicit PumpHarness(uint32_t pollIntervalMs = 60000,
                       uint32_t commandTimeoutMs = 3UL * 60UL * 1000UL)
    : modules(clock),
      device(clock, modules.mod1),
      mqtt(client, clock, pumpMqttConfig()),
      poller(modules.host, clock, pollIntervalMs, commandTimeoutMs),
      bridge(poller, mqtt, "watering/slot")
  {
    device.typeId = module_protocol::kTypePumpModule;
    modules.host.begin();
    mqtt.setBroker("broker.local", 1883);
    mqtt.begin();
    mqtt.update(true);
    mqtt.setMessageHandler(PumpMqttBridge::onMqttMessage, &bridge);
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
 * Reads the pump state once.
 *
 * @param harness Pump harness.
 * @return Nothing.
 */
void learnState(PumpHarness& harness)
{
  harness.poller.update();
}
}

void testCodecEncodesPumpCommands()
{
  uint8_t tx[8] = {};
  const size_t stateSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdGetPumpState, nullptr, 0, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(3, stateSize);
  TEST_ASSERT_EQUAL(2, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 2), tx[2]);

  const uint8_t desired = module_protocol::kPumpStateOn;
  const size_t setSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdSetPump, &desired, 1, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(4, setSize);
  TEST_ASSERT_EQUAL(3, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, tx[2]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 3), tx[3]);

  const size_t resetSize = ModuleCodec::encodeCommand(
      module_protocol::kCmdResetPump, nullptr, 0, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(3, resetSize);
  TEST_ASSERT_EQUAL(2, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 2), tx[2]);
}

void testHostReadsPumpStateSetAndReset()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateOff;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(module_protocol::kTypePumpModule, fixture.host.typeId(0));
  TEST_ASSERT_EQUAL_STRING("Pump", fixture.host.typeName(0));

  const PumpStateResult off = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Ok, off.status);
  TEST_ASSERT_EQUAL(PumpState::Off, off.state);
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState, lastCommand(fixture));

  device.pumpState = module_protocol::kPumpStateOn;
  const PumpStateResult on = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpState::On, on.state);
  device.pumpState = module_protocol::kPumpStateFault;
  const PumpStateResult fault = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpState::Fault, fault.state);

  device.pumpState = module_protocol::kPumpStateOff;
  const PumpStateResult turned = fixture.host.setPump(0, true);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Ok, turned.status);
  TEST_ASSERT_EQUAL(PumpState::On, turned.state);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(fixture));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, lastTx(fixture, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);

  device.pumpState = module_protocol::kPumpStateFault;
  const PumpStateResult stillFault = fixture.host.setPump(0, true);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Ok, stillFault.status);
  TEST_ASSERT_EQUAL(PumpState::Fault, stillFault.state);
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, device.pumpState);

  const PumpStateResult reset = fixture.host.resetPump(0);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Ok, reset.status);
  TEST_ASSERT_EQUAL(PumpState::Off, reset.state);
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, lastCommand(fixture));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, device.pumpState);

  device.pumpState = module_protocol::kPumpStateFault;
  device.pumpResetLeavesFault = true;
  const PumpStateResult stuck = fixture.host.resetPump(0);
  TEST_ASSERT_EQUAL(PumpState::Fault, stuck.state);
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, device.pumpState);

  device.pumpResetLeavesFault = false;
  device.pumpState = 9;
  const PumpStateResult bad = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Failed, bad.status);

  device.pumpState = module_protocol::kPumpStateOff;
  device.forceBusy = true;
  const PumpStateResult busy = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Busy, busy.status);
}

void testHostRejectsPumpQueryUnlessOnlinePump()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  const size_t before = fixture.bus.protocolOpCount();
  const PumpStateResult absent = fixture.host.queryPumpState(0);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected, absent.status);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected, fixture.host.setPump(0, true).status);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected, fixture.host.resetPump(0).status);
  TEST_ASSERT_EQUAL(before, fixture.bus.protocolOpCount());

  FakeModuleDevice device(clock, fixture.mod1);
  plugAndPump(fixture, 0, device);
  const size_t echo = fixture.bus.protocolOpCount();
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected,
                    fixture.host.queryPumpState(0).status);
  TEST_ASSERT_EQUAL(echo, fixture.bus.protocolOpCount());

  device.typeId = module_protocol::kTypeSensorModule;
  device.resetToUnconfigured();
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(module_protocol::kTypeSensorModule, fixture.host.typeId(0));
  const size_t sensor = fixture.bus.protocolOpCount();
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected,
                    fixture.host.queryPumpState(0).status);
  TEST_ASSERT_EQUAL(sensor, fixture.bus.protocolOpCount());

  device.typeId = module_protocol::kTypePumpModule;
  device.protocolVersion = 2;
  device.resetToUnconfigured();
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, fixture.host.state(0));
  const size_t unsupported = fixture.bus.protocolOpCount();
  TEST_ASSERT_EQUAL(PumpQueryStatus::Rejected,
                    fixture.host.setPump(0, false).status);
  TEST_ASSERT_EQUAL(unsupported, fixture.bus.protocolOpCount());
}

void testHostRejectsShortPumpPayload()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = module_protocol::kTypePumpModule;
  device.shortPumpPayload = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Failed, fixture.host.queryPumpState(0).status);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Failed, fixture.host.setPump(0, true).status);
  TEST_ASSERT_EQUAL(PumpQueryStatus::Failed, fixture.host.resetPump(0).status);
}

void testPumpTimingDefaultsAreThreeMinutesAndOneMinute()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  PumpPoller poller(fixture.host, clock);
  TEST_ASSERT_EQUAL(60000, poller.pollIntervalMs());
  TEST_ASSERT_EQUAL(3UL * 60UL * 1000UL, poller.commandTimeoutMs());
  PumpPoller zeros(fixture.host, clock, 0, 0);
  TEST_ASSERT_EQUAL(60000, zeros.pollIntervalMs());
  TEST_ASSERT_EQUAL(3UL * 60UL * 1000UL, zeros.commandTimeoutMs());
}

void testPollerReadsPumpStateThenRepeatsAtInterval()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  PumpState state = PumpState::Off;
  TEST_ASSERT_TRUE(harness.poller.pumpState(0, &state));
  TEST_ASSERT_EQUAL(PumpState::On, state);

  const size_t afterRead = harness.modules.bus.protocolOpCount();
  harness.clock.advance(59999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterRead, harness.modules.bus.protocolOpCount());
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterRead + 1, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
}

void testPollerRequeriesPumpStateAfterModuleReset()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  const uint32_t firstEpoch = harness.modules.host.identityEpoch(0);
  PumpState state = PumpState::Off;
  TEST_ASSERT_TRUE(harness.poller.pumpState(0, &state));
  TEST_ASSERT_EQUAL(PumpState::On, state);

  harness.device.pumpState = module_protocol::kPumpStateOff;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(harness.modules.host.identityEpoch(0) > firstEpoch);

  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  TEST_ASSERT_TRUE(harness.poller.pumpState(0, &state));
  TEST_ASSERT_EQUAL(PumpState::Off, state);
}

void testPollerRetriesBusyPumpStateThenWaits()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  harness.device.forceBusy = true;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    harness.poller.update();
    TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                      lastCommand(harness.modules));
  }
  const size_t afterGiveUp = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterGiveUp, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_FALSE(harness.poller.stateKnown(0));

  harness.clock.advance(59999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterGiveUp, harness.modules.bus.protocolOpCount());
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
}

void testPumpCommandSetsOnlyWhenStateDiffers()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  const size_t learned = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kPumpCommandTopic, "1 off");
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());

  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);

  const size_t afterOn = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterOn, harness.modules.bus.protocolOpCount());

  harness.client.deliver(kPumpCommandTopic, "1 off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);
}

void testPumpCommandBeforeStateReadsThenSets()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  const size_t plugged = harness.modules.bus.protocolOpCount();

  harness.client.deliver(kPumpCommandTopic, "1 on");
  TEST_ASSERT_EQUAL(plugged, harness.modules.bus.protocolOpCount());
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
}

void testPumpCommandAddressesSlotTwo()
{
  FakeClock clock;
  EmptyModuleHostFixture modules(clock);
  FakeModuleDevice device(clock, modules.mod(1));
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateOff;
  FakeMqttClient client;
  MqttService mqtt(client, clock, pumpMqttConfig());
  PumpPoller poller(modules.host, clock, 60000, 3UL * 60UL * 1000UL);
  PumpMqttBridge bridge(poller, mqtt, "watering/slot");
  modules.host.begin();
  mqtt.setBroker("broker.local", 1883);
  mqtt.begin();
  mqtt.update(true);
  mqtt.setMessageHandler(PumpMqttBridge::onMqttMessage, &bridge);
  plugAndPump(modules, 1, device);
  poller.update();
  const size_t learned = modules.bus.protocolOpCount();

  client.deliver(kPumpCommandTopic, "2 on");
  TEST_ASSERT_EQUAL(learned, modules.bus.protocolOpCount());
  poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, lastTx(modules, 2));
  bridge.update();
  TEST_ASSERT_TRUE(published(client, "watering/slot/2/pump", "on", true));
}

void testPumpFaultIsPublishedAndNotResetUntilCommand()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/pump", "fault",
                             true));

  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, harness.device.pumpState);
}

void testPumpResetThenTurnsOnWhenDesired()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));

  harness.client.deliver(kPumpCommandTopic, "1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
}

void testPumpResetThatStaysFaultDoesNotTurnOn()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateFault;
  harness.device.pumpResetLeavesFault = true;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.client.deliver(kPumpCommandTopic, "1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, harness.device.pumpState);
  const size_t afterReset = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterReset, harness.modules.bus.protocolOpCount());
}

void testPumpResetWhileOffDoesNotTurnOn()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 off");
  harness.poller.update();
  harness.client.deliver(kPumpCommandTopic, "1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, lastCommand(harness.modules));
  const size_t afterReset = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterReset, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);
}

void testPumpCommandRejectsMalformedPayload()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kPumpCommandTopic, "1");
  harness.client.deliver(kPumpCommandTopic, "on");
  harness.client.deliver(kPumpCommandTopic, "1 ON");
  harness.client.deliver(kPumpCommandTopic, "1 on off");
  harness.client.deliver(kPumpCommandTopic, "0 on");
  harness.client.deliver(kPumpCommandTopic, "5 off");
  harness.client.deliver(kPumpCommandTopic, "1 RESET");
  harness.client.deliver(kPumpCommandTopic, "1 reset now");
  harness.client.deliver(kSolenoidCommandTopic, "1 on");
  harness.client.deliver(kSensorReadTopic, "1 1");
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
}

void testPumpCommandDroppedForOtherModule()
{
  PumpHarness harness;
  harness.device.typeId = module_protocol::kTypeSolenoidModule;
  plugAndPump(harness.modules, 0, harness.device);
  const size_t online = harness.modules.bus.protocolOpCount();
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  harness.clock.advance(120000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(online, harness.modules.bus.protocolOpCount());
}

void testPumpCommandReappliesAfterModuleReset()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);

  harness.device.pumpState = module_protocol::kPumpStateOff;
  harness.device.resetToUnconfigured();
  pumpMs(harness.modules, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
}

void testPumpCommandTimeoutTurnsPumpOff()
{
  PumpHarness harness(60000, 5000);
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  const size_t learned = harness.modules.bus.protocolOpCount();

  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());

  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);

  const size_t afterOff = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterOff, harness.modules.bus.protocolOpCount());
}

void testPumpCommandRefreshesAbsenceTimeout()
{
  PumpHarness harness(60000, 5000);
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  const size_t afterMatch = harness.modules.bus.protocolOpCount();

  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterMatch, harness.modules.bus.protocolOpCount());

  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  harness.clock.advance(4999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(afterMatch, harness.modules.bus.protocolOpCount());

  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, lastTx(harness.modules, 2));
}

void testPumpResetDoesNotRefreshAbsenceTimeout()
{
  PumpHarness harness(60000, 5000);
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();

  harness.clock.advance(4000);
  harness.client.deliver(kPumpCommandTopic, "1 reset");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump, lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);

  harness.clock.advance(999);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, lastTx(harness.modules, 2));
}

void testPumpMalformedPayloadDoesNotRefreshAbsenceTimeout()
{
  PumpHarness harness(60000, 5000);
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.clock.advance(4999);
  harness.client.deliver(kPumpCommandTopic, "1 ON");
  harness.client.deliver(kPumpCommandTopic, "1 on off");
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
  harness.clock.advance(1);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);
}

void testPumpComingOnlineAfterTimeoutIsTurnedOff()
{
  PumpHarness harness(60000, 1000);
  harness.poller.update();
  harness.clock.advance(1000);
  harness.device.pumpState = module_protocol::kPumpStateOn;
  plugAndPump(harness.modules, 0, harness.device);
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, lastTx(harness.modules, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, harness.device.pumpState);
}

void testPumpTimeoutDoesNotResetFault()
{
  PumpHarness harness(60000, 1000);
  harness.device.pumpState = module_protocol::kPumpStateFault;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  const size_t learned = harness.modules.bus.protocolOpCount();
  harness.clock.advance(1000);
  harness.poller.update();
  TEST_ASSERT_EQUAL(learned, harness.modules.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, harness.device.pumpState);
}

void testPumpCommandWaitsThroughEnumeration()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  harness.modules.bus.attach(harness.device);
  harness.modules.sense(0).setPresent(true);
  pumpMs(harness.modules, 20);
  TEST_ASSERT_EQUAL(SlotState::Debouncing, harness.modules.host.state(0));
  harness.client.deliver(kPumpCommandTopic, "1 on");
  harness.poller.update();
  const size_t waiting = harness.modules.bus.protocolOpCount();
  harness.poller.update();
  TEST_ASSERT_EQUAL(waiting, harness.modules.bus.protocolOpCount());

  pumpMs(harness.modules, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, harness.modules.host.state(0));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(harness.modules));
  harness.poller.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(harness.modules));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, harness.device.pumpState);
}

void testPeriodicPumpPublishEachStateAndClearOnUnplug()
{
  PumpHarness harness;
  harness.device.pumpState = module_protocol::kPumpStateOff;
  plugAndPump(harness.modules, 0, harness.device);
  learnState(harness);
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/pump", "off",
                             true));

  harness.client.publishedMessages.clear();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(0, harness.client.publishedMessages.size());

  harness.device.pumpState = module_protocol::kPumpStateOn;
  harness.clock.advance(60000);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/pump", "on",
                             true));

  harness.client.publishedMessages.clear();
  harness.clock.advance(60000);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(1, harness.client.publishedMessages.size());
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/pump", "on",
                             true));

  harness.modules.sense(0).setPresent(false);
  pumpMs(harness.modules, 80);
  harness.poller.update();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, harness.modules.host.state(0));
  TEST_ASSERT_TRUE(published(harness.client, "watering/slot/1/pump",
                             "unavailable", true));
  const size_t once = harness.client.publishedMessages.size();
  harness.bridge.update();
  TEST_ASSERT_EQUAL(once, harness.client.publishedMessages.size());
}

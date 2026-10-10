#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ModuleBus.h"
#include "ModuleProtocol.h"
#include "Network.h"
#include "NetworkConfigRecord.h"
#include "Programming.h"
#include "PumpModule.h"
#include "SensorModule.h"
#include "SerialConsole.h"
#include "SolenoidModule.h"
#include "fakes/FakeBytePort.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeDigitalPin.h"
#include "fakes/FakeHalfDuplexUart.h"
#include "fakes/FakeI2cMaster.h"
#include "fakes/FakeModuleDevice.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNtpAdapter.h"
#include "fakes/FakePreferenceStore.h"
#include "fakes/FakeSerialPort.h"
#include "fakes/FakeSpiMaster.h"
#include "fakes/FakeWifi.h"

namespace
{

const NetworkTiming kTiming = {15000, 1000, 30000, 1000, 30000};
const NtpConfig kNtp = {
  "pool.ntp.org", "time.nist.gov", nullptr, 28800, 0, 60000};
const uint32_t kPollMs = 60000;
const uint32_t kSolenoidSilenceMs = 15UL * 60UL * 1000UL;
const uint32_t kPumpSilenceMs = 3UL * 60UL * 1000UL;
const uint8_t kStkEop = 0x20;
const uint8_t kStkInsync = 0x14;
const uint8_t kStkOk = 0x10;
const ProgrammingPins kProgrammingPins = {11, 13, 12, {6, 7, 15, 16}};

/**
 * Defaults matching startController(), with a broker so MQTT can connect.
 *
 * @return Configuration passed to Network::begin.
 */
NetworkConfig brokerDefaults()
{
  return {"", "", "broker.local", 1883, "watering-controller", nullptr,
          nullptr, "watering-controller", true, "watering"};
}

/**
 * One inbound delivery recorded by a test handler.
 */
struct Seen
{
  int calls = 0;
  std::string topic;
  std::string payload;
};

/**
 * Records one fan-out delivery.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @param context Seen instance.
 * @return Nothing.
 */
void remember(const char* topic, const uint8_t* payload, size_t length,
              void* context)
{
  Seen* seen = static_cast<Seen*>(context);
  seen->calls++;
  seen->topic = topic == nullptr ? "" : topic;
  if (payload != nullptr && length > 0)
  {
    seen->payload.assign(reinterpret_cast<const char*>(payload), length);
  }
  else
  {
    seen->payload.clear();
  }
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

/**
 * Counts publications of one topic.
 *
 * @param client Fake broker.
 * @param topic Topic name.
 * @return Number of matching messages.
 */
int countTopic(const FakeMqttClient& client, const char* topic)
{
  int count = 0;
  for (size_t i = 0; i < client.publishedMessages.size(); ++i)
  {
    if (client.publishedMessages[i].topic == topic)
    {
      count++;
    }
  }
  return count;
}

/**
 * Reads the command byte of the latest bus transaction.
 *
 * @param bus Fake I2C master.
 * @return Command byte, or 0 when the frame is short.
 */
uint8_t lastCommand(const FakeI2cMaster& bus)
{
  if (bus.ops.empty() || bus.ops.back().tx.size() < 2)
  {
    return 0;
  }
  return bus.ops.back().tx[1];
}

/**
 * Reads one byte of the latest request.
 *
 * @param bus Fake I2C master.
 * @param index Frame index.
 * @return Byte, or 0 when the frame is shorter.
 */
uint8_t txByte(const FakeI2cMaster& bus, size_t index)
{
  if (bus.ops.empty() || bus.ops.back().tx.size() <= index)
  {
    return 0;
  }
  return bus.ops.back().tx[index];
}

/**
 * Reports whether the serial output contains a line.
 *
 * @param serial Fake USB port.
 * @param line Expected line.
 * @return True when the line was written.
 */
bool wroteLine(const FakeSerialPort& serial, const char* line)
{
  for (size_t i = 0; i < serial.output.size(); ++i)
  {
    if (serial.output[i] == line)
    {
      return true;
    }
  }
  return false;
}

/**
 * Wi-Fi, NTP, MQTT, and the preference store, without daughter modules.
 */
struct NetKit
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter ntp;
  FakeMqttClient mqtt;
  FakePreferenceStore preferences;
  Network network;

  /**
   * Creates a network whose topic root is watering.
   */
  NetKit()
    : network(wifi, ntp, mqtt, clock, preferences, "watering", kNtp, kTiming)
  {
  }

  /**
   * Loads defaults and connects once the fake link is up.
   *
   * Begin reconfigures Wi-Fi, and that disconnect clears the fake link.
   * The link is restored after begin so the next update can associate.
   *
   * @return Nothing.
   */
  void connect()
  {
    network.begin(brokerDefaults());
    wifi.linkState = WifiLinkState::Connected;
    network.update();
  }
};

/**
 * Four slot pin triples and the I2C master they share.
 */
struct SlotRig
{
  FakeClock& clock;
  FakeDigitalPin sns1;
  FakeDigitalPin sns2;
  FakeDigitalPin sns3;
  FakeDigitalPin sns4;
  FakeDigitalPin mod1;
  FakeDigitalPin mod2;
  FakeDigitalPin mod3;
  FakeDigitalPin mod4;
  FakeDigitalPin cs1;
  FakeDigitalPin cs2;
  FakeDigitalPin cs3;
  FakeDigitalPin cs4;
  FakeI2cMaster i2c;
  SlotPins pins[4];

  /**
   * Binds twelve pins to one clock.
   *
   * @param clockRef Shared monotonic clock.
   */
  explicit SlotRig(FakeClock& clockRef)
    : clock(clockRef),
      pins{{sns1, mod1, cs1},
           {sns2, mod2, cs2},
           {sns3, mod3, cs3},
           {sns4, mod4, cs4}}
  {
  }

  /**
   * Returns the sense pin for a slot.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Sense pin.
   */
  FakeDigitalPin& sense(uint8_t slotIndex)
  {
    FakeDigitalPin* pinsSense[] = {&sns1, &sns2, &sns3, &sns4};
    return *pinsSense[slotIndex];
  }

  /**
   * Returns the MOD pin for a slot.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return MOD pin.
   */
  FakeDigitalPin& mod(uint8_t slotIndex)
  {
    FakeDigitalPin* pinsMod[] = {&mod1, &mod2, &mod3, &mod4};
    return *pinsMod[slotIndex];
  }
};

/**
 * The composition loop uses: network, bus, and the three type modules.
 */
struct Board
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter ntp;
  FakeMqttClient mqtt;
  FakePreferenceStore preferences;
  Network network;
  SlotRig slots;
  ModuleBus bus;
  SensorModule sensor;
  SolenoidModule solenoid;
  PumpModule pump;

  /**
   * Creates the board. Zero poll or silence values select the module default.
   *
   * @param sensorPollMs Sensor presence and reading period.
   * @param solenoidPollMs Solenoid state period.
   * @param solenoidSilenceMs Solenoid command-absence cutoff.
   * @param pumpPollMs Pump state period.
   * @param pumpSilenceMs Pump command-absence cutoff.
   */
  Board(uint32_t sensorPollMs, uint32_t solenoidPollMs,
        uint32_t solenoidSilenceMs, uint32_t pumpPollMs,
        uint32_t pumpSilenceMs)
    : network(wifi, ntp, mqtt, clock, preferences, "watering", kNtp, kTiming),
      slots(clock),
      bus(slots.i2c, clock, slots.pins, ModuleHostConfig{}, network),
      sensor(bus, network, clock, sensorPollMs),
      solenoid(bus, network, clock, solenoidPollMs, solenoidSilenceMs),
      pump(bus, network, clock, pumpPollMs, pumpSilenceMs)
  {
  }

  /**
   * Loads defaults, connects MQTT, and starts the bus.
   *
   * Begin reconfigures Wi-Fi, and that disconnect clears the fake link.
   * The link is restored after begin so the next update can associate.
   *
   * @return Nothing.
   */
  void start()
  {
    network.begin(brokerDefaults());
    wifi.linkState = WifiLinkState::Connected;
    network.update();
    bus.begin();
  }
};

/**
 * Programming object wired like main.cpp, with fakes for SPI and UPDI.
 * Programming is not movable, so it is constructed in place.
 */
struct ProgrammingRig
{
  FakeBytePort port;
  FakeSpiMaster spi;
  FakeHalfDuplexUart uart;
  Programming programming;

  /**
   * Binds the board's four CS pins and the shared pin numbers.
   *
   * @param board Composition under test.
   * @param idleMs Silence that ends the session.
   * @param unplugMs Seen-then-absent unplug limit.
   */
  ProgrammingRig(Board& board, uint32_t idleMs, uint32_t unplugMs)
    : programming(port, spi, uart, board.slots.cs1, board.slots.cs2,
                  board.slots.cs3, board.slots.cs4, kProgrammingPins,
                  board.bus, board.clock, idleMs, unplugMs)
  {
  }
};

/**
 * Result of pumping a module onto the bus.
 */
struct SeatResult
{
  bool online = false;
  bool oneTransactionPerUpdate = true;
};

/**
 * Seats a module and pumps ModuleBus::update until it is online.
 *
 * @param board Composition under test.
 * @param device Simulated daughter.
 * @param slotIndex Firmware slot.
 * @param onlineText Retained slot body that means online.
 * @param prefix Topic root currently applied to the bus.
 * @return Whether the slot published that body, and whether every
 *         update issued at most one I2C transaction.
 */
SeatResult seat(Board& board, FakeModuleDevice& device, uint8_t slotIndex,
                const char* onlineText, const char* prefix = "watering")
{
  SeatResult result;
  board.slots.i2c.attach(device);
  board.slots.sense(slotIndex).setPresent(true);
  char topic[40];
  std::snprintf(topic, sizeof(topic), "%s/slot/%u", prefix,
                static_cast<unsigned>(slotIndex + 1));
  for (uint32_t i = 0; i < 800; ++i)
  {
    board.clock.advance(1);
    const size_t before = board.slots.i2c.protocolOpCount();
    board.bus.update();
    if (board.slots.i2c.protocolOpCount() - before > 1)
    {
      result.oneTransactionPerUpdate = false;
    }
    if (published(board.mqtt, topic, onlineText, true))
    {
      result.online = true;
      return result;
    }
  }
  return result;
}

}


// ========== Network ==========

void testNetworkBeginAppliesDefaultsAndConfiguresNtp()
{
  NetKit kit;
  TEST_ASSERT_FALSE(kit.network.begin(brokerDefaults()));
  TEST_ASSERT_EQUAL_STRING("", kit.network.loadWarning());
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           kit.network.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           kit.network.config().mqttClientId);
  TEST_ASSERT_EQUAL_STRING("watering", kit.network.config().mqttPrefix);
  TEST_ASSERT_EQUAL_STRING("broker.local", kit.network.config().mqttHost);
  TEST_ASSERT_EQUAL(1883, kit.network.config().mqttPort);
  TEST_ASSERT_TRUE(kit.network.config().statusReporting);
  TEST_ASSERT_EQUAL_STRING("", kit.wifi.lastSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           kit.wifi.lastHostname.c_str());
  TEST_ASSERT_EQUAL(1, kit.ntp.configureCallCount);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", kit.ntp.lastServer1.c_str());
  TEST_ASSERT_EQUAL_STRING("time.nist.gov", kit.ntp.lastServer2.c_str());
  TEST_ASSERT_EQUAL_STRING("", kit.ntp.lastServer3.c_str());
  TEST_ASSERT_EQUAL(28800, kit.ntp.lastUtcOffsetSeconds);
  TEST_ASSERT_EQUAL(0, kit.ntp.lastDaylightOffsetSeconds);

  kit.preferences.strings[NetworkConfigKeys::wifiSsid] = "stored-net";
  NetKit loaded;
  loaded.preferences.strings[NetworkConfigKeys::wifiSsid] = "stored-net";
  TEST_ASSERT_TRUE(loaded.network.begin(brokerDefaults()));
  TEST_ASSERT_EQUAL_STRING("stored-net", loaded.network.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING(
    "Config: mqtt client id is not stored; using watering-controller",
    loaded.network.loadWarning());
  TEST_ASSERT_TRUE(
    loaded.preferences.contains(NetworkConfigKeys::mqttClientId));
}

void testNetworkUpdateConnectsMqttOnlyAfterWifi()
{
  NetKit kit;
  kit.network.begin(brokerDefaults());
  const int disconnectsAtBegin = kit.wifi.disconnectCallCount;
  kit.network.update();
  TEST_ASSERT_EQUAL(0, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL(0, kit.mqtt.subscribeCallCount);

  kit.clock.advance(14999);
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(disconnectsAtBegin, kit.wifi.disconnectCallCount);
  TEST_ASSERT_EQUAL(0, kit.mqtt.connectCallCount);

  kit.clock.advance(1);
  kit.network.update();
  TEST_ASSERT_TRUE(kit.wifi.disconnectCallCount > disconnectsAtBegin);
  TEST_ASSERT_EQUAL(0, kit.mqtt.connectCallCount);

  kit.clock.advance(999);
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.wifi.beginCallCount);

  kit.clock.advance(1);
  kit.wifi.linkState = WifiLinkState::Connected;
  kit.network.update();
  TEST_ASSERT_EQUAL(2, kit.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(0, kit.mqtt.connectCallCount);

  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           kit.mqtt.lastClientId.c_str());
  TEST_ASSERT_EQUAL_STRING("", kit.mqtt.lastUsername.c_str());
  TEST_ASSERT_EQUAL_STRING("broker.local", kit.mqtt.brokerHost.c_str());
  TEST_ASSERT_EQUAL(1883, kit.mqtt.brokerPort);
  TEST_ASSERT_EQUAL(5, kit.mqtt.subscribeCallCount);
  TEST_ASSERT_EQUAL_STRING("watering/solenoids",
                           kit.mqtt.subscribedTopics[0].c_str());
  TEST_ASSERT_EQUAL_STRING("watering/solenoids/connected",
                           kit.mqtt.subscribedTopics[1].c_str());
  TEST_ASSERT_EQUAL_STRING("watering/pump",
                           kit.mqtt.subscribedTopics[2].c_str());
  TEST_ASSERT_EQUAL_STRING("watering/sensor/read",
                           kit.mqtt.subscribedTopics[3].c_str());
  TEST_ASSERT_EQUAL_STRING("watering/sensor/connected",
                           kit.mqtt.subscribedTopics[4].c_str());
  TEST_ASSERT_EQUAL(1, kit.mqtt.subscribedQos[0]);

  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL(1, kit.mqtt.loopCallCount);
}

void testNetworkReconnectsAfterWifiDropUsingBackoff()
{
  NetKit kit;
  kit.connect();
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL(1, kit.wifi.beginCallCount);
  const int disconnectsAfterConnect = kit.mqtt.disconnectCallCount;

  kit.wifi.linkState = WifiLinkState::Disconnected;
  kit.network.update();
  TEST_ASSERT_EQUAL(disconnectsAfterConnect + 1, kit.mqtt.disconnectCallCount);
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);

  kit.clock.advance(999);
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);

  kit.clock.advance(1);
  kit.wifi.linkState = WifiLinkState::Connected;
  kit.network.update();
  TEST_ASSERT_EQUAL(2, kit.wifi.beginCallCount);
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);

  kit.network.update();
  TEST_ASSERT_EQUAL(2, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL(10, kit.mqtt.subscribeCallCount);
}

void testNetworkPublishFollowsBrokerConnection()
{
  NetKit kit;
  kit.network.begin(brokerDefaults());
  TEST_ASSERT_FALSE(kit.network.publish("watering/slot/1", "Empty", true));
  TEST_ASSERT_EQUAL(0, kit.mqtt.publishCallCount);

  kit.wifi.linkState = WifiLinkState::Connected;
  kit.network.update();
  TEST_ASSERT_TRUE(kit.network.publish("watering/slot/1", "Empty", true));
  TEST_ASSERT_EQUAL(1, kit.mqtt.publishedMessages.size());
  TEST_ASSERT_TRUE(published(kit.mqtt, "watering/slot/1", "Empty", true));

  kit.mqtt.publishResult = false;
  TEST_ASSERT_FALSE(kit.network.publish("watering/slot/1", "again", false));
  TEST_ASSERT_EQUAL(2, kit.mqtt.publishCallCount);
}

void testNetworkFansInboundMessagesOutToFourHandlers()
{
  NetKit kit;
  kit.connect();
  Seen seen[4];
  for (int i = 0; i < 4; ++i)
  {
    TEST_ASSERT_TRUE(kit.network.addMessageHandler(remember, &seen[i]));
  }
  TEST_ASSERT_FALSE(kit.network.addMessageHandler(remember, &seen[0]));
  TEST_ASSERT_FALSE(kit.network.addMessageHandler(nullptr, &seen[0]));

  kit.mqtt.deliver("watering/sensor/read", "1 1");
  for (int i = 0; i < 4; ++i)
  {
    TEST_ASSERT_EQUAL(1, seen[i].calls);
    TEST_ASSERT_EQUAL_STRING("watering/sensor/read", seen[i].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("1 1", seen[i].payload.c_str());
  }

  kit.mqtt.deliver("watering/pump", "1 off");
  TEST_ASSERT_EQUAL(2, seen[0].calls);
  TEST_ASSERT_EQUAL_STRING("1 off", seen[0].payload.c_str());
}

void testNetworkApplyPersistsThroughPreferenceStore()
{
  NetKit kit;
  TEST_ASSERT_FALSE(kit.network.begin(brokerDefaults()));
  NetworkConfig staged = brokerDefaults();
  staged.wifiSsid = "garden";
  staged.wifiHostname = "plant-room";
  staged.mqttPrefix = "shed";
  NetworkConfigFieldMask fields;
  fields.wifiSsid = true;
  fields.wifiHostname = true;
  fields.mqttPrefix = true;
  TEST_ASSERT_TRUE(kit.network.apply(staged, fields));
  TEST_ASSERT_EQUAL_STRING("garden", kit.network.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("plant-room", kit.network.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("shed", kit.network.config().mqttPrefix);
  TEST_ASSERT_EQUAL_STRING("garden",
                           kit.preferences.strings[NetworkConfigKeys::wifiSsid]
                             .c_str());

  NetKit again;
  again.preferences.strings = kit.preferences.strings;
  again.preferences.ports = kit.preferences.ports;
  TEST_ASSERT_TRUE(again.network.begin(brokerDefaults()));
  TEST_ASSERT_EQUAL_STRING("garden", again.network.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("plant-room", again.network.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("shed", again.network.config().mqttPrefix);
  TEST_ASSERT_EQUAL_STRING("broker.local", again.network.config().mqttHost);
}

void testNetworkRejectsInvalidNamesAndAClosedStore()
{
  TEST_ASSERT_TRUE(Network::isValidHostname("plant-room"));
  TEST_ASSERT_TRUE(Network::isValidHostname("A1"));
  TEST_ASSERT_FALSE(Network::isValidHostname(nullptr));
  TEST_ASSERT_FALSE(Network::isValidHostname(""));
  TEST_ASSERT_FALSE(Network::isValidHostname("-bad"));
  TEST_ASSERT_FALSE(Network::isValidHostname("bad-"));
  TEST_ASSERT_FALSE(Network::isValidHostname("has space"));
  TEST_ASSERT_FALSE(Network::isValidHostname(
    "abcdefghijklmnopqrstuvwxyz012345"));

  NetKit kit;
  kit.network.begin(brokerDefaults());
  NetworkConfig bad = brokerDefaults();
  bad.wifiHostname = "-bad";
  NetworkConfigFieldMask hostname;
  hostname.wifiHostname = true;
  TEST_ASSERT_FALSE(kit.network.apply(bad, hostname));
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           kit.network.config().wifiHostname);
  TEST_ASSERT_FALSE(kit.preferences.contains(NetworkConfigKeys::wifiHostname));

  bad.mqttPrefix = "bad-";
  NetworkConfigFieldMask prefix;
  prefix.mqttPrefix = true;
  TEST_ASSERT_FALSE(kit.network.apply(bad, prefix));
  TEST_ASSERT_EQUAL_STRING("watering", kit.network.config().mqttPrefix);

  NetKit closed;
  closed.preferences.openResult = false;
  TEST_ASSERT_FALSE(closed.network.begin(brokerDefaults()));
  TEST_ASSERT_EQUAL_STRING("watering", closed.network.config().mqttPrefix);
  TEST_ASSERT_EQUAL_STRING("", closed.network.loadWarning());
}

void testNetworkEmptyBrokerStaysUnconfiguredUntilApplied()
{
  NetKit kit;
  NetworkConfig emptyHost = brokerDefaults();
  emptyHost.mqttHost = "";
  kit.network.begin(emptyHost);
  kit.wifi.linkState = WifiLinkState::Connected;
  kit.network.update();
  kit.network.update();
  TEST_ASSERT_EQUAL(0, kit.mqtt.connectCallCount);

  NetworkConfig withHost = brokerDefaults();
  NetworkConfigFieldMask fields;
  fields.mqttHost = true;
  TEST_ASSERT_TRUE(kit.network.apply(withHost, fields));
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.mqtt.connectCallCount);
  TEST_ASSERT_EQUAL_STRING("broker.local", kit.mqtt.brokerHost.c_str());
}

void testNetworkRetriesNtpOnItsInterval()
{
  NetKit kit;
  kit.wifi.linkState = WifiLinkState::Connected;
  kit.network.begin(brokerDefaults());
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.ntp.configureCallCount);
  kit.clock.advance(59999);
  kit.network.update();
  TEST_ASSERT_EQUAL(1, kit.ntp.configureCallCount);
  kit.clock.advance(1);
  kit.network.update();
  TEST_ASSERT_EQUAL(2, kit.ntp.configureCallCount);
  TEST_ASSERT_EQUAL(28800, kit.ntp.lastUtcOffsetSeconds);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", kit.ntp.lastServer1.c_str());
}


// ========== Module bus ==========

void testModuleBusPublishesEmptySlotsOnceWhenMqttConnects()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.bus.begin();
  board.bus.update();
  TEST_ASSERT_EQUAL(0, board.mqtt.publishedMessages.size());
  TEST_ASSERT_EQUAL(1, board.slots.i2c.beginCallCount);

  board.network.begin(brokerDefaults());
  board.wifi.linkState = WifiLinkState::Connected;
  board.network.update();
  board.bus.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1", "Empty", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2", "Empty", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/3", "Empty", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/4", "Empty", true));
  TEST_ASSERT_EQUAL(1, countTopic(board.mqtt, "watering/slot/1"));

  board.bus.update();
  TEST_ASSERT_EQUAL(1, countTopic(board.mqtt, "watering/slot/1"));
  TEST_ASSERT_EQUAL(0, board.slots.i2c.protocolOpCount());
}

void testModuleBusEnumeratesWithOneTransactionPerUpdate()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  const SeatResult seated =
    seat(board, device, 0, "Online Sensor addr=0x10");
  TEST_ASSERT_TRUE(seated.online);
  TEST_ASSERT_TRUE(seated.oneTransactionPerUpdate);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2", "Empty", true));
  TEST_ASSERT_EQUAL(0x10, board.slots.i2c.ops.back().address);
}

void testModuleBusFollowsAPrefixChangeAndQuiesces()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  board.bus.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1", "Empty", true));

  NetworkConfig staged = brokerDefaults();
  staged.mqttPrefix = "shed";
  NetworkConfigFieldMask fields;
  fields.mqttPrefix = true;
  TEST_ASSERT_TRUE(board.network.apply(staged, fields));
  board.network.update();
  const char* last =
    board.mqtt.subscribedTopics.back().c_str();
  TEST_ASSERT_EQUAL_STRING("shed/sensor/connected", last);
  board.bus.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "shed/slot/1", "Empty", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "shed/slot/4", "Empty", true));

  const size_t ops = board.slots.i2c.protocolOpCount();
  board.bus.quiesce();
  for (int i = 0; i < 5; ++i)
  {
    board.clock.advance(1);
    board.bus.update();
  }
  TEST_ASSERT_EQUAL(ops, board.slots.i2c.protocolOpCount());
  board.bus.resume();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeIdentityEcho;
  const SeatResult seated =
    seat(board, device, 0, "Online IdentityEcho addr=0x10", "shed");
  TEST_ASSERT_TRUE(seated.online);
  TEST_ASSERT_TRUE(seated.oneTransactionPerUpdate);
}


// ========== Sensor ==========

void testSensorModulePublishesReadingOnTheSameUpdate()
{
  Board board(0, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 1;
  device.sensorConnected[0] = true;
  device.sensorValue[0] = 10;
  const SeatResult seated =
    seat(board, device, 0, "Online Sensor addr=0x10");
  TEST_ASSERT_TRUE(seated.online);

  const size_t plugged = board.slots.i2c.protocolOpCount();
  board.sensor.update();
  TEST_ASSERT_EQUAL(plugged + 1, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_FALSE(published(board.mqtt, "watering/slot/1/sensor/1",
                              "connected 10", true));

  board.sensor.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(board.slots.i2c));
  board.sensor.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/1",
                             "connected 10", true));
  const int once = countTopic(board.mqtt, "watering/slot/1/sensor/1");
  board.sensor.update();
  TEST_ASSERT_EQUAL(once, countTopic(board.mqtt, "watering/slot/1/sensor/1"));
}

void testSensorModuleReadCommandAndMalformedPayload()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  Seen extra;
  TEST_ASSERT_TRUE(board.network.addMessageHandler(remember, &extra));
  TEST_ASSERT_FALSE(board.network.addMessageHandler(remember, &extra));

  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 1;
  device.sensorConnected[0] = true;
  device.sensorValue[0] = 10;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Sensor addr=0x10").online);
  board.sensor.update();
  board.sensor.update();
  board.sensor.update();
  board.mqtt.publishedMessages.clear();

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/sensor/read", "1 1");
  TEST_ASSERT_EQUAL(1, extra.calls);
  TEST_ASSERT_EQUAL_STRING("1 1", extra.payload.c_str());
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());

  board.sensor.update();
  TEST_ASSERT_EQUAL(learned + 1, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/1",
                             "connected 10", true));

  board.mqtt.publishedMessages.clear();
  board.mqtt.deliver("watering/sensor/read", "nope");
  board.mqtt.deliver("watering/sensor/read", "1");
  board.mqtt.deliver("watering/sensor/read", "0 1");
  board.mqtt.deliver("watering/sensor/read", "1 0");
  board.mqtt.deliver("watering/sensor/read", "5 1");
  board.mqtt.deliver("watering/pump", "1 on");
  const size_t afterRejects = board.slots.i2c.protocolOpCount();
  board.sensor.update();
  TEST_ASSERT_EQUAL(afterRejects, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(0, board.mqtt.publishedMessages.size());

  board.mqtt.deliver("watering/sensor/read", "1 2");
  board.sensor.update();
  TEST_ASSERT_EQUAL(afterRejects, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/2",
                             "unavailable", false));
}

void testSensorModuleHonorsPollIntervalAndUnplug()
{
  Board board(0, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 1;
  device.sensorConnected[0] = false;
  device.sensorValue[0] = 0;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Sensor addr=0x10").online);
  board.sensor.update();
  board.sensor.update();
  board.sensor.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/1",
                             "disconnected", true));

  const size_t afterCycle = board.slots.i2c.protocolOpCount();
  board.clock.advance(59999);
  board.sensor.update();
  TEST_ASSERT_EQUAL(afterCycle, board.slots.i2c.protocolOpCount());

  device.sensorConnected[0] = true;
  device.sensorValue[0] = -5;
  board.clock.advance(1);
  board.sensor.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorConnected,
                    lastCommand(board.slots.i2c));
  board.sensor.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorReading,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/1",
                             "connected -5", true));

  board.slots.sense(0).setPresent(false);
  for (uint32_t i = 0; i < 80; ++i)
  {
    board.clock.advance(1);
    board.bus.update();
  }
  board.sensor.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensor/1",
                             "unavailable", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensors",
                             "unavailable", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1", "Empty", true));
}

void testSensorModulePublishesConnectedList()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 4;
  device.sensorConnected[0] = true;
  device.sensorConnected[1] = true;
  device.sensorConnected[2] = false;
  device.sensorConnected[3] = true;
  device.sensorValue[0] = 1;
  device.sensorValue[1] = 2;
  device.sensorValue[2] = 0;
  device.sensorValue[3] = 4;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Sensor addr=0x10").online);

  for (int step = 0; step < 9; ++step)
  {
    board.sensor.update();
  }
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/sensors",
                             "4 1 2 4", true));
  const int once = countTopic(board.mqtt, "watering/slot/1/sensors");
  board.sensor.update();
  TEST_ASSERT_EQUAL(once, countTopic(board.mqtt, "watering/slot/1/sensors"));

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/sensor/connected", "1");
  board.sensor.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(once + 1, countTopic(board.mqtt, "watering/slot/1/sensors"));

  board.mqtt.publishedMessages.clear();
  board.mqtt.deliver("watering/sensor/connected", "1 2");
  board.mqtt.deliver("watering/sensor/connected", "5");
  board.mqtt.deliver("watering/sensor/connected", "on");
  board.mqtt.deliver("watering/sensor/connected", "");
  board.sensor.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(0, board.mqtt.publishedMessages.size());
}

void testSensorModuleDoesNotExchangeWhileProgramming()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSensorModule;
  device.sensorCount = 1;
  device.sensorConnected[0] = true;
  device.sensorValue[0] = 10;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Sensor addr=0x10").online);

  ProgrammingRig rig(board, 60000, 1000);
  const size_t ops = board.slots.i2c.protocolOpCount();
  rig.programming.begin(1, ProgrammingMethod::Isp);
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);
  TEST_ASSERT_TRUE(board.slots.cs1.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, board.slots.mod1.mode);
  for (int i = 0; i < 3; ++i)
  {
    board.bus.update();
    board.sensor.update();
  }
  TEST_ASSERT_EQUAL(ops, board.slots.i2c.protocolOpCount());

  board.clock.advance(60000);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);
  board.sensor.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSensorCount,
                    lastCommand(board.slots.i2c));
}


// ========== Solenoid ==========

void testSolenoidModuleCommandPublishesAndTimesOut()
{
  Board board(kPollMs, 0, 0, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSolenoidModule;
  device.solenoidCount = 1;
  device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  TEST_ASSERT_TRUE(
    seat(board, device, 0, "Online Solenoid addr=0x10").online);

  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidCount,
                    lastCommand(board.slots.i2c));
  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetSolenoidState,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/1",
                             "off", true));

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/solenoids", "1 maybe");
  board.mqtt.deliver("watering/solenoids", "1");
  board.solenoid.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());

  board.mqtt.deliver("watering/solenoids", "1 on");
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());
  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(0, txByte(board.slots.i2c, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn,
                    txByte(board.slots.i2c, 3));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn,
                    device.solenoidState[0]);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/1",
                             "on", true));

  board.clock.advance(kSolenoidSilenceMs - 1);
  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOn, device.solenoidState[0]);
  board.clock.advance(1);
  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff,
                    txByte(board.slots.i2c, 3));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff, device.solenoidState[0]);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/1",
                             "off", true));
}

void testSolenoidModuleSkipsDisconnectedAndPublishesInventory()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeSolenoidModule;
  device.solenoidCount = 2;
  device.solenoidState[0] = module_protocol::kSolenoidStateOn;
  device.solenoidState[1] = module_protocol::kSolenoidStateDisconnected;
  TEST_ASSERT_TRUE(
    seat(board, device, 0, "Online Solenoid addr=0x10").online);
  board.solenoid.update();
  board.solenoid.update();
  board.solenoid.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/1",
                             "on", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/2",
                             "disconnected", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoids",
                             "2 1", true));

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/solenoids/connected", "1");
  board.solenoid.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());

  board.mqtt.deliver("watering/solenoids", "1 off off");
  board.solenoid.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetSolenoid,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(0, txByte(board.slots.i2c, 2));
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateOff,
                    device.solenoidState[0]);
  const size_t afterSet = board.slots.i2c.protocolOpCount();
  board.solenoid.update();
  TEST_ASSERT_EQUAL(afterSet, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kSolenoidStateDisconnected,
                    device.solenoidState[1]);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/solenoid/1",
                             "off", true));
}

void testSolenoidModuleClearsOnUnplug()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod2);
  device.typeId = module_protocol::kTypeSolenoidModule;
  device.solenoidCount = 1;
  device.solenoidState[0] = module_protocol::kSolenoidStateOff;
  TEST_ASSERT_TRUE(
    seat(board, device, 1, "Online Solenoid addr=0x11").online);
  board.mqtt.deliver("watering/solenoids", "2 on");
  bool setOutput = false;
  for (int i = 0; i < 4; ++i)
  {
    board.solenoid.update();
    if (lastCommand(board.slots.i2c) == module_protocol::kCmdSetSolenoid)
    {
      setOutput = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(setOutput);
  TEST_ASSERT_EQUAL(0x11, board.slots.i2c.ops.back().address);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2/solenoid/1",
                             "on", true));

  board.slots.sense(1).setPresent(false);
  for (uint32_t i = 0; i < 80; ++i)
  {
    board.clock.advance(1);
    board.bus.update();
  }
  board.solenoid.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2/solenoid/1",
                             "unavailable", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2/solenoids",
                             "unavailable", true));
}


// ========== Pump ==========

void testPumpModuleTurnsOnAndPublishes()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, 0, 0);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateOff;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Pump addr=0x10").online);

  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "off", true));

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/pump", "1");
  board.mqtt.deliver("watering/pump", "1 ON");
  board.mqtt.deliver("watering/pump", "1 on off");
  board.pump.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());

  board.mqtt.deliver("watering/pump", "1 off");
  board.pump.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());

  board.mqtt.deliver("watering/pump", "1 on");
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, txByte(board.slots.i2c, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "on", true));

  const size_t afterOn = board.slots.i2c.protocolOpCount();
  board.clock.advance(kPumpSilenceMs - 1);
  board.mqtt.deliver("watering/pump", "1 ON");
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);
  board.clock.advance(1);
  board.pump.update();
  TEST_ASSERT_TRUE(board.slots.i2c.protocolOpCount() > afterOn);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, txByte(board.slots.i2c, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, device.pumpState);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "off", true));
}

void testPumpModuleResetsFaultBeforeTurningOn()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateFault;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Pump addr=0x10").online);
  board.pump.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "fault",
                             true));

  const size_t learned = board.slots.i2c.protocolOpCount();
  board.mqtt.deliver("watering/pump", "1 on");
  board.pump.update();
  TEST_ASSERT_EQUAL(learned, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, device.pumpState);

  board.mqtt.deliver("watering/pump", "1 reset");
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, device.pumpState);
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "on", true));

  device.pumpState = module_protocol::kPumpStateFault;
  device.pumpResetLeavesFault = true;
  device.resetToUnconfigured();
  board.mqtt.publishedMessages.clear();
  bool onlineAgain = false;
  for (uint32_t i = 0; i < 4000; ++i)
  {
    board.clock.advance(1);
    board.bus.update();
    if (published(board.mqtt, "watering/slot/1", "Online Pump addr=0x10",
                  true))
    {
      onlineAgain = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(onlineAgain);
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdGetPumpState,
                    lastCommand(board.slots.i2c));
  board.mqtt.deliver("watering/pump", "1 on");
  board.mqtt.deliver("watering/pump", "1 reset");
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump,
                    lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateFault, device.pumpState);
  const size_t afterReset = board.slots.i2c.protocolOpCount();
  board.pump.update();
  TEST_ASSERT_EQUAL(afterReset, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "fault",
                             true));
}

void testPumpModuleResetDoesNotRefreshAbsenceTimeout()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, 0);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateOn;
  TEST_ASSERT_TRUE(seat(board, device, 0, "Online Pump addr=0x10").online);
  board.pump.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "on", true));
  board.mqtt.deliver("watering/pump", "1 on");
  board.pump.update();

  board.clock.advance(120000);
  board.mqtt.deliver("watering/pump", "1 reset");
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdResetPump,
                    lastCommand(board.slots.i2c));
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);

  board.clock.advance(59999);
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOn, device.pumpState);
  board.clock.advance(1);
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, txByte(board.slots.i2c, 2));
  TEST_ASSERT_EQUAL(module_protocol::kPumpStateOff, device.pumpState);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/1/pump", "off", true));
}

void testPumpModuleAddressesSlotTwoAndClearsOnUnplug()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod(1));
  device.typeId = module_protocol::kTypePumpModule;
  device.pumpState = module_protocol::kPumpStateOff;
  TEST_ASSERT_TRUE(seat(board, device, 1, "Online Pump addr=0x11").online);
  board.mqtt.deliver("watering/pump", "2 on");
  board.pump.update();
  board.pump.update();
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetPump, lastCommand(board.slots.i2c));
  TEST_ASSERT_EQUAL(0x11, board.slots.i2c.ops.back().address);
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2/pump", "on", true));

  board.slots.sense(1).setPresent(false);
  for (uint32_t i = 0; i < 80; ++i)
  {
    board.clock.advance(1);
    board.bus.update();
  }
  board.pump.update();
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2/pump",
                             "unavailable", true));
  TEST_ASSERT_TRUE(published(board.mqtt, "watering/slot/2", "Empty", true));
}


// ========== Programming ==========

void testProgrammingBeginQuiescesUntilIdleTimeout()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  ProgrammingRig rig(board, 60000, 1000);
  rig.port.plugged = false;
  rig.programming.begin(1, ProgrammingMethod::Isp);
  TEST_ASSERT_TRUE(rig.programming.active());
  board.clock.advance(59999);
  rig.programming.update();
  board.bus.update();
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(0, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);

  board.clock.advance(1);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);

  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeIdentityEcho;
  const SeatResult seated =
    seat(board, device, 0, "Online IdentityEcho addr=0x10");
  TEST_ASSERT_TRUE(seated.online);
  TEST_ASSERT_TRUE(seated.oneTransactionPerUpdate);
}

void testProgrammingSyncRefreshesIdleAndUnplugEndsSession()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeIdentityEcho;
  TEST_ASSERT_TRUE(
    seat(board, device, 0, "Online IdentityEcho addr=0x10").online);
  const size_t ops = board.slots.i2c.protocolOpCount();

  ProgrammingRig rig(board, 60000, 1000);
  rig.programming.begin(1, ProgrammingMethod::Isp);
  rig.programming.update();
  const uint8_t sync[] = {0x30, kStkEop};
  rig.port.feed(sync, sizeof(sync));
  rig.programming.update();
  TEST_ASSERT_EQUAL(2, rig.port.output.size());
  TEST_ASSERT_EQUAL(kStkInsync, rig.port.output[0]);
  TEST_ASSERT_EQUAL(kStkOk, rig.port.output[1]);

  board.clock.advance(59999);
  board.bus.update();
  rig.programming.update();
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(ops, board.slots.i2c.protocolOpCount());

  board.clock.advance(1);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);

  rig.port.output.clear();
  rig.programming.begin(1, ProgrammingMethod::Isp);
  rig.programming.update();
  rig.port.plugged = false;
  rig.programming.update();
  board.clock.advance(999);
  rig.programming.update();
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);
  board.clock.advance(1);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);

  board.clock.advance(1);
  board.bus.update();
  TEST_ASSERT_TRUE(board.slots.i2c.protocolOpCount() > ops);
}

void testProgrammingIspUsesSelectedSlotAndLeavesTheOthers()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);
  board.slots.cs1.setMode(PinMode::DigitalOutput);
  board.slots.cs1.write(true);
  const int cs1Writes = board.slots.cs1.writeCount;

  ProgrammingRig rig(board, 60000, 1000);
  rig.programming.begin(3, ProgrammingMethod::Isp);
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs3.mode);
  TEST_ASSERT_TRUE(board.slots.cs3.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);
  TEST_ASSERT_EQUAL(cs1Writes, board.slots.cs1.writeCount);
  TEST_ASSERT_EQUAL(0, board.slots.i2c.protocolOpCount());

  const int writesBefore = board.slots.cs3.writeCount;
  const int attachBefore = rig.uart.attachCount;
  rig.programming.begin(3, ProgrammingMethod::Isp);
  TEST_ASSERT_EQUAL(writesBefore, board.slots.cs3.writeCount);
  TEST_ASSERT_EQUAL(attachBefore, rig.uart.attachCount);

  rig.spi.script = {0x00, 0x00, 0x53, 0x00};
  const uint8_t enter[] = {0x50, kStkEop};
  rig.port.feed(enter, sizeof(enter));
  rig.programming.update();
  TEST_ASSERT_EQUAL(1, rig.spi.beginCount);
  TEST_ASSERT_EQUAL(125000, rig.spi.clockHz);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs3.mode);
  TEST_ASSERT_FALSE(board.slots.cs3.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);

  board.clock.advance(60000);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs3.mode);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, board.slots.cs1.mode);
  TEST_ASSERT_EQUAL(cs1Writes, board.slots.cs1.writeCount);
}

/**
 * AVR067 CRC-16 over a buffer. Init 0xFFFF, reflected polynomial 0xA001.
 *
 * @param data Bytes to cover.
 * @param length Number of bytes.
 * @return CRC value.
 */
uint16_t crc16Jtag(const uint8_t* data, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index)
  {
    crc = static_cast<uint16_t>(crc ^ data[index]);
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if ((crc & 0x0001) != 0)
      {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      }
      else
      {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

/**
 * Builds one jtagice frame.
 *
 * @param sequence Sequence number.
 * @param body Command body.
 * @param length Body length.
 * @return Frame bytes, including the CRC.
 */
std::vector<uint8_t> jtagFrame(uint16_t sequence, const uint8_t* body,
                               size_t length)
{
  std::vector<uint8_t> frame;
  frame.push_back(0x1B);
  frame.push_back(static_cast<uint8_t>(sequence & 0xFF));
  frame.push_back(static_cast<uint8_t>(sequence >> 8));
  frame.push_back(static_cast<uint8_t>(length & 0xFF));
  frame.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
  frame.push_back(0);
  frame.push_back(0);
  frame.push_back(0x0E);
  for (size_t index = 0; index < length; ++index)
  {
    frame.push_back(body[index]);
  }
  const uint16_t crc = crc16Jtag(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>(crc >> 8));
  return frame;
}

void testProgrammingUpdiClaimsOnlyTheSelectedPin()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  ProgrammingRig rig(board, 60000, 1000);
  rig.programming.begin(2, ProgrammingMethod::Updi);
  TEST_ASSERT_TRUE(rig.programming.active());
  TEST_ASSERT_EQUAL(0, rig.spi.beginCount);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs2.mode);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);
  TEST_ASSERT_EQUAL(0, rig.uart.attachCount);

  uint8_t body[247] = {};
  body[0] = 0x0C;
  body[244] = 64;
  const std::vector<uint8_t> frame = jtagFrame(1, body, sizeof(body));
  rig.port.feed(frame.data(), frame.size());
  rig.programming.update();
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, board.slots.cs2.mode);
  TEST_ASSERT_FALSE(board.slots.cs2.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);

  const int writesAtBreak = board.slots.cs2.writeCount;
  const int attachesAtBreak = rig.uart.attachCount;
  rig.programming.begin(2, ProgrammingMethod::Updi);
  TEST_ASSERT_EQUAL(writesAtBreak, board.slots.cs2.writeCount);
  TEST_ASSERT_EQUAL(attachesAtBreak, rig.uart.attachCount);
  TEST_ASSERT_EQUAL(0, rig.spi.beginCount);

  board.clock.advance(25);
  rig.programming.update();
  TEST_ASSERT_TRUE(board.slots.cs2.level);
  board.clock.advance(1);
  rig.programming.update();
  TEST_ASSERT_FALSE(board.slots.cs2.level);
  board.clock.advance(25);
  rig.programming.update();
  TEST_ASSERT_TRUE(board.slots.cs2.level);
  TEST_ASSERT_TRUE(rig.uart.attached);
  TEST_ASSERT_EQUAL(7, rig.uart.gpio);
  TEST_ASSERT_EQUAL(115200, rig.uart.baud);
  TEST_ASSERT_EQUAL(1, rig.uart.attachCount);
  TEST_ASSERT_EQUAL(0, rig.spi.beginCount);

  const int attaches = rig.uart.attachCount;
  const int detaches = rig.uart.detachCount;
  rig.programming.begin(1, ProgrammingMethod::Isp);
  TEST_ASSERT_EQUAL(attaches, rig.uart.attachCount);
  TEST_ASSERT_EQUAL(detaches, rig.uart.detachCount);
  TEST_ASSERT_TRUE(rig.uart.attached);

  board.clock.advance(60000);
  rig.programming.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs2.mode);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);
}

void testProgrammingUpdiSilenceAndUnplugEndTheSession()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  ProgrammingRig quiet(board, 60000, 1000);
  quiet.port.plugged = false;
  quiet.programming.begin(4, ProgrammingMethod::Updi);
  board.clock.advance(59999);
  quiet.programming.update();
  TEST_ASSERT_TRUE(quiet.programming.active());
  board.clock.advance(1);
  quiet.programming.update();
  TEST_ASSERT_FALSE(quiet.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs4.mode);

  ProgrammingRig unplug(board, 60000, 1000);
  unplug.programming.begin(4, ProgrammingMethod::Updi);
  unplug.programming.update();
  unplug.port.plugged = false;
  unplug.programming.update();
  board.clock.advance(999);
  unplug.programming.update();
  TEST_ASSERT_TRUE(unplug.programming.active());
  board.clock.advance(1);
  unplug.programming.update();
  TEST_ASSERT_FALSE(unplug.programming.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs4.mode);
}

void testProgrammingRejectsSlotOutsideOneToFour()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.start();
  FakeModuleDevice device(board.clock, board.slots.mod1);
  device.typeId = module_protocol::kTypeIdentityEcho;
  TEST_ASSERT_TRUE(
    seat(board, device, 0, "Online IdentityEcho addr=0x10").online);
  const size_t ops = board.slots.i2c.protocolOpCount();

  ProgrammingRig rig(board, 60000, 1000);
  rig.programming.begin(0, ProgrammingMethod::Isp);
  TEST_ASSERT_FALSE(rig.programming.active());
  rig.programming.begin(5, ProgrammingMethod::Updi);
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_EQUAL(ops, board.slots.i2c.protocolOpCount());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, board.slots.cs1.mode);
  TEST_ASSERT_EQUAL(0, rig.spi.beginCount);
  TEST_ASSERT_EQUAL(0, rig.uart.attachCount);

  const ModuleHostConfig host;
  board.clock.advance(host.healthPingMs);
  board.bus.update();
  TEST_ASSERT_FALSE(rig.programming.active());
  TEST_ASSERT_TRUE(board.slots.i2c.protocolOpCount() > ops);
  TEST_ASSERT_EQUAL(0, rig.spi.beginCount);
}


// ========== Serial console ==========

void testSerialConsoleApplyPersistsAndStatusPrintsImmediately()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.network.begin(brokerDefaults());
  board.bus.begin();
  FakeSerialPort serial;
  SerialConsole console(serial, board.clock, board.network, board.bus, 10000,
                      kProgrammingPins);
  console.begin();
  serial.output.clear();

  serial.feed("status\n");
  console.update();
  TEST_ASSERT_TRUE(wroteLine(serial, "WiFi Status: Connecting"));
  const size_t afterStatus = serial.output.size();
  TEST_ASSERT_TRUE(afterStatus > 1);
  console.updateStatus();
  TEST_ASSERT_EQUAL(afterStatus, serial.output.size());

  serial.feed("set wifi.ssid garden\n");
  serial.feed("set wifi.hostname plant-room\n");
  serial.feed("set mqtt.prefix shed\n");
  serial.feed("apply\n");
  console.update();
  TEST_ASSERT_TRUE(wroteLine(serial, "OK applied"));
  TEST_ASSERT_EQUAL_STRING("garden", board.network.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("plant-room", board.network.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("shed", board.network.config().mqttPrefix);
  TEST_ASSERT_EQUAL_STRING(
    "garden",
    board.preferences.strings[NetworkConfigKeys::wifiSsid].c_str());

  board.wifi.linkState = WifiLinkState::Connected;
  board.network.update();
  board.network.update();
  bool sawShed = false;
  for (size_t i = 0; i < board.mqtt.subscribedTopics.size(); ++i)
  {
    if (board.mqtt.subscribedTopics[i] == "shed/sensor/read")
    {
      sawShed = true;
    }
  }
  TEST_ASSERT_TRUE(sawShed);

  NetKit again;
  again.preferences.strings = board.preferences.strings;
  again.preferences.ports = board.preferences.ports;
  TEST_ASSERT_TRUE(again.network.begin(brokerDefaults()));
  TEST_ASSERT_EQUAL_STRING("garden", again.network.config().wifiSsid);
  TEST_ASSERT_EQUAL_STRING("plant-room", again.network.config().wifiHostname);
  TEST_ASSERT_EQUAL_STRING("shed", again.network.config().mqttPrefix);
}

void testSerialConsolePeriodicStatusAndProgramRequest()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.network.begin(brokerDefaults());
  board.bus.begin();
  FakeSerialPort serial;
  SerialConsole console(serial, board.clock, board.network, board.bus, 10000,
                      kProgrammingPins);
  console.begin();
  serial.output.clear();

  console.updateStatus();
  TEST_ASSERT_EQUAL(0, serial.output.size());
  board.clock.advance(9999);
  console.updateStatus();
  TEST_ASSERT_EQUAL(0, serial.output.size());
  board.clock.advance(1);
  console.updateStatus();
  TEST_ASSERT_TRUE(wroteLine(serial, "WiFi Status: Connecting"));
  const size_t afterPeriodic = serial.output.size();

  serial.feed("set status off\napply\n");
  console.update();
  TEST_ASSERT_FALSE(board.network.config().statusReporting);
  TEST_ASSERT_TRUE(wroteLine(serial, "OK applied"));
  board.clock.advance(10000);
  console.updateStatus();
  const size_t afterSilent = serial.output.size();
  TEST_ASSERT_EQUAL(afterPeriodic + 2, afterSilent);

  serial.feed("status\n");
  console.update();
  TEST_ASSERT_TRUE(serial.output.size() > afterSilent);
  TEST_ASSERT_TRUE(wroteLine(serial, "WiFi Status: Connecting"));

  serial.plugged = false;
  const size_t beforeUnplugged = serial.output.size();
  serial.feed("status\n");
  console.update();
  console.updateStatus();
  TEST_ASSERT_EQUAL(beforeUnplugged, serial.output.size());

  serial.plugged = true;
  serial.output.clear();
  serial.feed("program 1 isp\n");
  console.update();
  ProgrammingRequest request;
  TEST_ASSERT_TRUE(console.takeProgrammingRequest(request));
  TEST_ASSERT_FALSE(console.takeProgrammingRequest(request));
  TEST_ASSERT_EQUAL(1, request.slot);
  TEST_ASSERT_EQUAL(static_cast<int>(ProgrammingMethod::Isp),
                    static_cast<int>(request.method));
  TEST_ASSERT_TRUE(wroteLine(serial, "OK programming"));
  TEST_ASSERT_TRUE(wroteLine(
    serial,
    "ISP slot 1: MOSI GPIO11, MISO GPIO13, SCK GPIO12, RESET GPIO6, 3V3, GND"));

  serial.output.clear();
  serial.feed("program 4 updi\n");
  console.update();
  TEST_ASSERT_TRUE(console.takeProgrammingRequest(request));
  TEST_ASSERT_EQUAL(4, request.slot);
  TEST_ASSERT_EQUAL(static_cast<int>(ProgrammingMethod::Updi),
                    static_cast<int>(request.method));
  TEST_ASSERT_TRUE(wroteLine(serial, "UPDI slot 4: UPDI GPIO16, 3V3, GND"));

  serial.output.clear();
  serial.feed("program updi\n");
  console.update();
  TEST_ASSERT_FALSE(console.takeProgrammingRequest(request));
  TEST_ASSERT_TRUE(wroteLine(serial, "ERR program"));
}

void testSerialConsoleRejectsBadHostnameWhileUnplugged()
{
  Board board(kPollMs, kPollMs, kSolenoidSilenceMs, kPollMs, kPumpSilenceMs);
  board.network.begin(brokerDefaults());
  board.bus.begin();
  FakeSerialPort serial;
  SerialConsole console(serial, board.clock, board.network, board.bus, 10000,
                      kProgrammingPins);
  console.begin();

  serial.plugged = false;
  serial.feed("set wifi.hostname -bad\napply\n");
  console.update();
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           board.network.config().wifiHostname);
  TEST_ASSERT_EQUAL(0, serial.output.size());

  serial.plugged = true;
  console.update();
  TEST_ASSERT_TRUE(wroteLine(serial, "ERR hostname"));
  TEST_ASSERT_EQUAL_STRING("watering-controller",
                           board.network.config().wifiHostname);
  TEST_ASSERT_FALSE(
    board.preferences.contains(NetworkConfigKeys::wifiHostname));
  TEST_ASSERT_TRUE(wroteLine(serial, "OK applied"));
}


// ========== Runner ==========

/**
 * Registers the seven-module interface cases.
 *
 * @return Nothing.
 */
void runDeepModuleTests()
{
  // UNITY_BEGIN records test_main.cpp, so name this file for the report.
  UnitySetTestFile(__FILE__);
  RUN_TEST(testNetworkBeginAppliesDefaultsAndConfiguresNtp);
  RUN_TEST(testNetworkUpdateConnectsMqttOnlyAfterWifi);
  RUN_TEST(testNetworkReconnectsAfterWifiDropUsingBackoff);
  RUN_TEST(testNetworkPublishFollowsBrokerConnection);
  RUN_TEST(testNetworkFansInboundMessagesOutToFourHandlers);
  RUN_TEST(testNetworkApplyPersistsThroughPreferenceStore);
  RUN_TEST(testNetworkRejectsInvalidNamesAndAClosedStore);
  RUN_TEST(testNetworkEmptyBrokerStaysUnconfiguredUntilApplied);
  RUN_TEST(testNetworkRetriesNtpOnItsInterval);
  RUN_TEST(testModuleBusPublishesEmptySlotsOnceWhenMqttConnects);
  RUN_TEST(testModuleBusEnumeratesWithOneTransactionPerUpdate);
  RUN_TEST(testModuleBusFollowsAPrefixChangeAndQuiesces);
  RUN_TEST(testSensorModulePublishesReadingOnTheSameUpdate);
  RUN_TEST(testSensorModuleReadCommandAndMalformedPayload);
  RUN_TEST(testSensorModuleHonorsPollIntervalAndUnplug);
  RUN_TEST(testSensorModulePublishesConnectedList);
  RUN_TEST(testSensorModuleDoesNotExchangeWhileProgramming);
  RUN_TEST(testSolenoidModuleCommandPublishesAndTimesOut);
  RUN_TEST(testSolenoidModuleSkipsDisconnectedAndPublishesInventory);
  RUN_TEST(testSolenoidModuleClearsOnUnplug);
  RUN_TEST(testPumpModuleTurnsOnAndPublishes);
  RUN_TEST(testPumpModuleResetsFaultBeforeTurningOn);
  RUN_TEST(testPumpModuleResetDoesNotRefreshAbsenceTimeout);
  RUN_TEST(testPumpModuleAddressesSlotTwoAndClearsOnUnplug);
  RUN_TEST(testProgrammingBeginQuiescesUntilIdleTimeout);
  RUN_TEST(testProgrammingSyncRefreshesIdleAndUnplugEndsSession);
  RUN_TEST(testProgrammingIspUsesSelectedSlotAndLeavesTheOthers);
  RUN_TEST(testProgrammingUpdiClaimsOnlyTheSelectedPin);
  RUN_TEST(testProgrammingUpdiSilenceAndUnplugEndTheSession);
  RUN_TEST(testProgrammingRejectsSlotOutsideOneToFour);
  RUN_TEST(testSerialConsoleApplyPersistsAndStatusPrintsImmediately);
  RUN_TEST(testSerialConsolePeriodicStatusAndProgramRequest);
  RUN_TEST(testSerialConsoleRejectsBadHostnameWhileUnplugged);
}

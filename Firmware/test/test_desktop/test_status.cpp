#include <unity.h>

#include "MqttService.h"
#include "NtpService.h"
#include "SerialStatusReporter.h"
#include "WifiManager.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeModuleDevice.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNtpAdapter.h"
#include "fakes/FakeSerialPort.h"
#include "fakes/FakeWifi.h"

namespace
{
WifiManagerConfig wifiConfig()
{
  return {"garden", "secret", 1000, 100, 400};
}

NtpConfig ntpConfig(int32_t utcOffsetSeconds, int32_t daylightOffsetSeconds)
{
  return {"pool.ntp.org", "time.nist.gov", nullptr, utcOffsetSeconds,
          daylightOffsetSeconds, 500};
}

MqttConfig mqttConfig()
{
  return {"watering-controller", nullptr, nullptr, nullptr, 0, 100, 400};
}

struct StatusFixture
{
  FakeClock clock;
  EmptyModuleHostFixture modules;
  FakeWifi wifi;
  FakeNtpAdapter ntpAdapter;
  FakeMqttClient mqttClient;
  FakeSerialPort serial;
  WifiManager wifiManager;
  NtpService ntpService;
  MqttService mqttService;
  SerialStatusReporter reporter;

  StatusFixture()
    : StatusFixture(ntpConfig(28800, 0))
  {
  }

  explicit StatusFixture(const NtpConfig& ntp)
    : modules(clock),
      wifiManager(wifi, clock, wifiConfig()),
      ntpService(ntpAdapter, clock, ntp),
      mqttService(mqttClient, clock, mqttConfig()),
      reporter(serial, clock, wifiManager, ntpService, mqttService,
               modules.host)
  {
  }

  /**
   * Starts status reporting with the supplied snapshot interval.
   *
   * @param intervalMs Snapshot interval in milliseconds.
   * @return Nothing.
   */
  void start(uint32_t intervalMs = 1000)
  {
    reporter.begin({intervalMs});
  }
};
}

void testWifiManagerForwardsRssi()
{
  FakeClock clock;
  FakeWifi wifi;
  wifi.rssiDbm = -55;
  WifiManager manager(wifi, clock, wifiConfig());

  TEST_ASSERT_EQUAL(-55, manager.rssi());
}

void testNtpServiceExposesConfig()
{
  FakeClock clock;
  FakeNtpAdapter adapter;
  NtpService service(adapter, clock, ntpConfig(28800, 0));

  TEST_ASSERT_EQUAL(28800, service.config().utcOffsetSeconds);
  TEST_ASSERT_EQUAL(0, service.config().daylightOffsetSeconds);
  TEST_ASSERT_EQUAL(500, service.config().retryIntervalMs);
}

void testStatusReporterWritesNothingBeforeBegin()
{
  StatusFixture fixture;

  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(0, fixture.serial.output.size());
}

void testStatusReporterWritesNothingWhenUnplugged()
{
  StatusFixture fixture;
  fixture.serial.plugged = false;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(0, fixture.serial.output.size());
}

void testStatusReporterPrintsIdleStates()
{
  StatusFixture fixture;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Idle",
                           fixture.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Idle",
                           fixture.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("MQTT Status: Idle",
                           fixture.serial.output[2].c_str());
  TEST_ASSERT_EQUAL_STRING("Slot 1: Empty", fixture.serial.output[3].c_str());
  TEST_ASSERT_EQUAL_STRING("Slot 2: Empty", fixture.serial.output[4].c_str());
  TEST_ASSERT_EQUAL_STRING("Slot 3: Empty", fixture.serial.output[5].c_str());
  TEST_ASSERT_EQUAL_STRING("Slot 4: Empty", fixture.serial.output[6].c_str());
}

void testStatusReporterPrintsConnectingWithoutRssi()
{
  StatusFixture fixture;
  fixture.start();

  fixture.wifiManager.begin();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting",
                           fixture.serial.output[0].c_str());
}

void testStatusReporterPrintsBackoffWithoutRssi()
{
  StatusFixture fixture;
  fixture.start();

  fixture.wifiManager.begin();
  fixture.clock.advance(1000);
  fixture.wifiManager.update();
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, fixture.wifiManager.state());
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Backoff",
                           fixture.serial.output[0].c_str());
}

void testStatusReporterPrintsConnectedRssi()
{
  StatusFixture fixture;
  fixture.wifi.rssiDbm = -67;
  fixture.start();

  fixture.wifiManager.begin();
  fixture.wifi.linkState = WifiLinkState::Connected;
  fixture.wifiManager.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connected (-67 dBm)",
                           fixture.serial.output[0].c_str());
}

void testStatusReporterPrintsWaitingForSyncWithoutTime()
{
  StatusFixture fixture;
  fixture.start();

  fixture.ntpService.begin();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: WaitingForSync",
                           fixture.serial.output[1].c_str());
}

void testStatusReporterPrintsSynchronizedLocalTime()
{
  StatusFixture fixture;
  fixture.start();

  fixture.ntpService.begin();
  fixture.ntpAdapter.synchronized = true;
  fixture.ntpAdapter.epoch = 1700000000;
  fixture.ntpService.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:20)",
                           fixture.serial.output[1].c_str());
}

void testStatusReporterAppliesDaylightOffset()
{
  StatusFixture fixture(ntpConfig(0, 3600));
  fixture.start();

  fixture.ntpService.begin();
  fixture.ntpAdapter.synchronized = true;
  fixture.ntpAdapter.epoch = 1700000000;
  fixture.ntpService.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-14 23:13:20)",
                           fixture.serial.output[1].c_str());
}

void testStatusReporterPrintsMqttWaitingForNetwork()
{
  StatusFixture fixture;
  fixture.start();

  fixture.mqttService.setBroker("broker.local", 1883);
  fixture.mqttService.begin();
  fixture.mqttService.update(false);
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL_STRING("MQTT Status: WaitingForNetwork",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterPrintsMqttConnected()
{
  StatusFixture fixture;
  fixture.start();

  fixture.mqttService.setBroker("broker.local", 1883);
  fixture.mqttService.begin();
  fixture.mqttService.update(true);
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL_STRING("MQTT Status: Connected",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterPrintsMqttBackoff()
{
  StatusFixture fixture;
  fixture.mqttClient.connectResult = false;
  fixture.start();

  fixture.mqttService.setBroker("broker.local", 1883);
  fixture.mqttService.begin();
  fixture.mqttService.update(true);
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(MqttServiceState::Backoff, fixture.mqttService.state());
  TEST_ASSERT_EQUAL_STRING("MQTT Status: Backoff",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterPrintsMqttUnconfigured()
{
  StatusFixture fixture;
  fixture.start();

  fixture.mqttService.setBroker("", 1883);
  fixture.mqttService.begin();
  fixture.mqttService.update(true);
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(MqttServiceState::Unconfigured, fixture.mqttService.state());
  TEST_ASSERT_EQUAL(0, fixture.mqttClient.connectCallCount);
  TEST_ASSERT_EQUAL_STRING("MQTT Status: Unconfigured",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterUsesConfiguredInterval()
{
  StatusFixture fixture;
  fixture.start(250);

  fixture.clock.advance(249);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(0, fixture.serial.output.size());

  fixture.clock.advance(1);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
}

void testStatusReporterExposesConfig()
{
  StatusFixture fixture;
  fixture.start(250);

  TEST_ASSERT_EQUAL(250, fixture.reporter.config().intervalMs);
}

void testStatusReporterReconfigureChangesInterval()
{
  StatusFixture fixture;
  fixture.start(1000);

  fixture.clock.advance(1000);
  fixture.reporter.update();
  const size_t afterFirst = fixture.serial.output.size();

  fixture.reporter.reconfigure({200});
  TEST_ASSERT_EQUAL(200, fixture.reporter.config().intervalMs);

  fixture.clock.advance(199);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(afterFirst, fixture.serial.output.size());

  fixture.clock.advance(1);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(afterFirst + 7, fixture.serial.output.size());
}

void testStatusReporterWaitsForIntervalBeforeReprint()
{
  StatusFixture fixture;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();
  const size_t afterFirst = fixture.serial.output.size();

  fixture.clock.advance(999);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(afterFirst, fixture.serial.output.size());

  fixture.clock.advance(1);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(afterFirst + 7, fixture.serial.output.size());
}

void testStatusReporterAdvancesClockAfterSync()
{
  StatusFixture fixture;
  fixture.start();

  fixture.ntpService.begin();
  fixture.ntpAdapter.synchronized = true;
  fixture.ntpAdapter.epoch = 1700000000;
  fixture.ntpService.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  fixture.ntpAdapter.epoch = 1700000001;
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(14, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:20)",
                           fixture.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:21)",
                           fixture.serial.output[8].c_str());
}

void testStatusReporterStopsWhenUnpluggedAfterPrint()
{
  StatusFixture fixture;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());

  fixture.serial.plugged = false;
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
}

void testStatusReporterPrintsEmptySlots()
{
  StatusFixture fixture;
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL_STRING("Slot 1: Empty", fixture.serial.output[3].c_str());
  TEST_ASSERT_EQUAL_STRING("Slot 4: Empty", fixture.serial.output[6].c_str());
}

void testStatusReporterSlotOneIsIndexZeroAddr10()
{
  StatusFixture fixture;
  FakeModuleDevice device(fixture.clock, fixture.modules.mod1);
  fixture.modules.host.begin();
  fixture.modules.bus.attach(device);
  fixture.modules.sns1.setPresent(true);
  for (uint32_t i = 0; i < 800; ++i)
  {
    fixture.clock.advance(1);
    fixture.modules.host.update();
  }
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL_STRING("Slot 1: Online IdentityEcho addr=0x10",
                           fixture.serial.output[3].c_str());
}

void testStatusReporterPrintsOnlineSlot()
{
  testStatusReporterSlotOneIsIndexZeroAddr10();
}

void testStatusReporterPrintsFault()
{
  StatusFixture fixture;
  fixture.modules.host.begin();
  fixture.modules.sns1.setPresent(true);
  for (uint32_t i = 0; i < 800; ++i)
  {
    fixture.clock.advance(1);
    fixture.modules.host.update();
  }
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL_STRING("Slot 1: Fault Nack",
                           fixture.serial.output[3].c_str());
}

void testSerialStatusDoesNotCallPingOrEcho()
{
  StatusFixture fixture;
  FakeModuleDevice device(fixture.clock, fixture.modules.mod1);
  fixture.modules.host.begin();
  fixture.modules.bus.attach(device);
  fixture.modules.sns1.setPresent(true);
  for (uint32_t i = 0; i < 800; ++i)
  {
    fixture.clock.advance(1);
    fixture.modules.host.update();
  }
  const size_t before = fixture.modules.bus.protocolOpCount();
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(before, fixture.modules.bus.protocolOpCount());
}

void testStatusReporterPrintsConnectedAddressAndRssi()
{
  StatusFixture fixture;
  fixture.wifi.rssiDbm = -67;
  fixture.wifi.localAddress = {{192, 168, 4, 20}};
  fixture.start();

  fixture.wifiManager.begin();
  fixture.wifi.linkState = WifiLinkState::Connected;
  fixture.wifiManager.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connected (192.168.4.20, -67 dBm)",
                           fixture.serial.output[0].c_str());
}

void testStatusReporterOmitsAddressUntilConnected()
{
  StatusFixture fixture;
  fixture.wifi.localAddress = {{10, 1, 2, 3}};
  fixture.wifi.rssiDbm = -40;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Idle",
                           fixture.serial.output[0].c_str());

  fixture.wifiManager.begin();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting",
                           fixture.serial.output[7].c_str());

  fixture.wifiManager.update();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, fixture.wifiManager.state());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Backoff",
                           fixture.serial.output[14].c_str());
}

void testStatusReporterSkipsPrintsWhenDisabled()
{
  StatusFixture fixture;
  fixture.start();
  TEST_ASSERT_TRUE(fixture.reporter.reportingEnabled());

  fixture.reporter.setReportingEnabled(false);
  TEST_ASSERT_EQUAL(1000, fixture.reporter.config().intervalMs);
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(0, fixture.serial.output.size());
}

void testStatusReporterResumeWaitsForFullInterval()
{
  StatusFixture fixture;
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());

  fixture.reporter.setReportingEnabled(false);
  fixture.clock.advance(5000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());

  fixture.reporter.setReportingEnabled(true);
  fixture.clock.advance(999);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());

  fixture.clock.advance(1);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(14, fixture.serial.output.size());
}

void testStatusReporterPrintsOnDemandWhileDisabled()
{
  StatusFixture fixture;
  fixture.start();
  fixture.reporter.setReportingEnabled(false);
  fixture.reporter.printStatus();

  TEST_ASSERT_EQUAL(7, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Idle",
                           fixture.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Idle",
                           fixture.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("MQTT Status: Idle",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterPrintOnDemandWritesNothingWhenUnplugged()
{
  StatusFixture fixture;
  fixture.serial.plugged = false;
  fixture.reporter.printStatus();
  TEST_ASSERT_EQUAL(0, fixture.serial.output.size());
}

void testStatusReporterPrintOnDemandRestartsInterval()
{
  StatusFixture fixture;
  fixture.start();
  fixture.clock.advance(1000);
  fixture.reporter.update();
  fixture.clock.advance(400);
  fixture.reporter.printStatus();
  TEST_ASSERT_EQUAL(14, fixture.serial.output.size());

  fixture.clock.advance(999);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(14, fixture.serial.output.size());

  fixture.clock.advance(1);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(21, fixture.serial.output.size());
}

void testWifiManagerForwardsLocalAddress()
{
  FakeClock clock;
  FakeWifi wifi;
  wifi.localAddress = {{192, 168, 4, 20}};
  WifiManager manager(wifi, clock, wifiConfig());

  const Ipv4Address address = manager.localIp();
  TEST_ASSERT_EQUAL(192, address.octets[0]);
  TEST_ASSERT_EQUAL(168, address.octets[1]);
  TEST_ASSERT_EQUAL(4, address.octets[2]);
  TEST_ASSERT_EQUAL(20, address.octets[3]);
}

void testWifiManagerCommitsChangedHostnameBeforeBegin()
{
  FakeClock clock;
  FakeWifi wifi;
  WifiManager manager(wifi, clock,
                      {"garden", "secret", 1000, 100, 400, nullptr});
  manager.begin();
  TEST_ASSERT_EQUAL(0, wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL(0, wifi.setHostnameCount);
  TEST_ASSERT_EQUAL(1, wifi.beginCallCount);

  manager.reconfigure({"garden", "secret", 1000, 100, 400, "plant-room"});
  manager.begin();
  TEST_ASSERT_EQUAL(1, wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL(1, wifi.setHostnameCount);
  TEST_ASSERT_EQUAL_STRING("plant-room", wifi.lastHostname.c_str());
  TEST_ASSERT_EQUAL_STRING("reset", wifi.calls[1].c_str());
  TEST_ASSERT_EQUAL_STRING("hostname", wifi.calls[2].c_str());
  TEST_ASSERT_EQUAL_STRING("begin", wifi.calls[3].c_str());

  clock.advance(1000);
  manager.update();
  clock.advance(100);
  manager.update();
  TEST_ASSERT_EQUAL(1, wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL(1, wifi.setHostnameCount);
  TEST_ASSERT_EQUAL(3, wifi.beginCallCount);

  manager.reconfigure({"garden", "secret", 1000, 100, 400, "plant-room"});
  manager.begin();
  TEST_ASSERT_EQUAL(1, wifi.resetStationModeCount);

  manager.reconfigure({"garden", "secret", 1000, 100, 400, "tank-room"});
  manager.begin();
  TEST_ASSERT_EQUAL(2, wifi.resetStationModeCount);
  TEST_ASSERT_EQUAL_STRING("tank-room", wifi.lastHostname.c_str());
}

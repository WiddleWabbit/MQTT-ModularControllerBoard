#include <unity.h>

#include "MqttService.h"
#include "NtpService.h"
#include "SerialStatusReporter.h"
#include "WifiManager.h"
#include "fakes/FakeClock.h"
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
    : wifiManager(wifi, clock, wifiConfig()),
      ntpService(ntpAdapter, clock, ntp),
      mqttService(mqttClient, clock, mqttConfig()),
      reporter(serial, clock, wifiManager, ntpService, mqttService)
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

  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Idle",
                           fixture.serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Idle",
                           fixture.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("MQTT Status: Idle",
                           fixture.serial.output[2].c_str());
}

void testStatusReporterPrintsConnectingWithoutRssi()
{
  StatusFixture fixture;
  fixture.start();

  fixture.wifiManager.begin();
  fixture.clock.advance(1000);
  fixture.reporter.update();

  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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
  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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

  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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

  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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

  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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
  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
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
  TEST_ASSERT_EQUAL(afterFirst + 3, fixture.serial.output.size());
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
  TEST_ASSERT_EQUAL(afterFirst + 3, fixture.serial.output.size());
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

  TEST_ASSERT_EQUAL(6, fixture.serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:20)",
                           fixture.serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:21)",
                           fixture.serial.output[4].c_str());
}

void testStatusReporterStopsWhenUnpluggedAfterPrint()
{
  StatusFixture fixture;
  fixture.start();

  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());

  fixture.serial.plugged = false;
  fixture.clock.advance(1000);
  fixture.reporter.update();
  TEST_ASSERT_EQUAL(3, fixture.serial.output.size());
}

#include <unity.h>

#include "NtpService.h"
#include "SerialStatusReporter.h"
#include "WifiManager.h"
#include "fakes/FakeClock.h"
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

void testStatusReporterWritesNothingWhenUnplugged()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  serial.plugged = false;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(0, serial.output.size());
}

void testStatusReporterPrintsIdleStates()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Idle", serial.output[0].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Idle", serial.output[1].c_str());
}

void testStatusReporterPrintsConnectingWithoutRssi()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  wifiManager.begin();
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connecting", serial.output[0].c_str());
}

void testStatusReporterPrintsBackoffWithoutRssi()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  wifiManager.begin();
  clock.advance(1000);
  wifiManager.update();
  reporter.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, wifiManager.state());
  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Backoff", serial.output[0].c_str());
}

void testStatusReporterPrintsConnectedRssi()
{
  FakeClock clock;
  FakeWifi wifi;
  wifi.rssiDbm = -67;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  wifiManager.begin();
  wifi.linkState = WifiLinkState::Connected;
  wifiManager.update();
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("WiFi Status: Connected (-67 dBm)",
                           serial.output[0].c_str());
}

void testStatusReporterPrintsWaitingForSyncWithoutTime()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  ntpService.begin();
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: WaitingForSync",
                           serial.output[1].c_str());
}

void testStatusReporterPrintsSynchronizedLocalTime()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  ntpService.begin();
  adapter.synchronized = true;
  adapter.epoch = 1700000000;
  ntpService.update();
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(2, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:20)",
                           serial.output[1].c_str());
}

void testStatusReporterAppliesDaylightOffset()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(0, 3600));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  ntpService.begin();
  adapter.synchronized = true;
  adapter.epoch = 1700000000;
  ntpService.update();
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-14 23:13:20)",
                           serial.output[1].c_str());
}

void testStatusReporterWaitsForIntervalBeforeReprint()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  clock.advance(1000);
  reporter.update();
  const size_t afterFirst = serial.output.size();

  clock.advance(999);
  reporter.update();
  TEST_ASSERT_EQUAL(afterFirst, serial.output.size());

  clock.advance(1);
  reporter.update();
  TEST_ASSERT_EQUAL(afterFirst + 2, serial.output.size());
}

void testStatusReporterAdvancesClockAfterSync()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  ntpService.begin();
  adapter.synchronized = true;
  adapter.epoch = 1700000000;
  ntpService.update();
  clock.advance(1000);
  reporter.update();

  adapter.epoch = 1700000001;
  clock.advance(1000);
  reporter.update();

  TEST_ASSERT_EQUAL(4, serial.output.size());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:20)",
                           serial.output[1].c_str());
  TEST_ASSERT_EQUAL_STRING("NTP Status: Synchronized (2023-11-15 06:13:21)",
                           serial.output[3].c_str());
}

void testStatusReporterStopsWhenUnpluggedAfterPrint()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter adapter;
  FakeSerialPort serial;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(adapter, clock, ntpConfig(28800, 0));
  SerialStatusReporter reporter(serial, clock, wifiManager, ntpService, 1000);

  clock.advance(1000);
  reporter.update();
  TEST_ASSERT_EQUAL(2, serial.output.size());

  serial.plugged = false;
  clock.advance(1000);
  reporter.update();
  TEST_ASSERT_EQUAL(2, serial.output.size());
}

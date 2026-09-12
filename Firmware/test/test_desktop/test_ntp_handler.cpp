#include <unity.h>

#include "NtpHandler.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNtpClient.h"
#include "fakes/FakeWifiStation.h"

namespace {
FakeClock fakeClock;
FakeWifiStation wifi;
FakeNtpClient ntp;

void resetFixtures()
{
  fakeClock.currentMillis = 0;
  wifi.connected = false;
  ntp.reset();
}
}

void test_defaultsToPoolServerAndThirtyMinuteFrequency()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", handler.server());
  TEST_ASSERT_EQUAL_UINT32(1800000UL, handler.updateFrequencyMs());
}

void test_requestsSynchronizationOnceWhenWifiFirstConnects()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  handler.update();
  TEST_ASSERT_EQUAL_UINT32(0, ntp.requestCount);

  wifi.connected = true;
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(1, ntp.requestCount);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", ntp.lastServer);
  TEST_ASSERT_EQUAL_UINT32(1, ntp.updateCount);
}

void test_doesNotRepeatRequestBeforeFrequencyExpires()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  fakeClock.advance(999);
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(1, ntp.requestCount);
  TEST_ASSERT_EQUAL_UINT32(2, ntp.updateCount);
}

void test_requestsAgainWhenFrequencyExpires()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  fakeClock.advance(1000);
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(2, ntp.requestCount);
}

void test_reconnectionTriggersAnImmediateSynchronization()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp, "time.example", 1000);
  wifi.connected = true;
  handler.update();
  wifi.connected = false;
  handler.update();
  wifi.connected = true;
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(2, ntp.requestCount);
}

void test_updateIsNonblockingAndContinuesDrivingNtpClient()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(2, ntp.updateCount);
}

void test_configurationCanBeChanged()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  handler.setServer("custom.example");
  handler.setUpdateFrequencyMs(2500);

  TEST_ASSERT_EQUAL_STRING("custom.example", handler.server());
  TEST_ASSERT_EQUAL_UINT32(2500, handler.updateFrequencyMs());
}

void test_emptyServerIsRejected()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  TEST_ASSERT_FALSE(handler.setServer(""));
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", handler.server());
}

void test_zeroFrequencyIsRejected()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  TEST_ASSERT_FALSE(handler.setUpdateFrequencyMs(0));
  TEST_ASSERT_EQUAL_UINT32(1800000UL, handler.updateFrequencyMs());
}

void test_reportsSynchronizationState()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp, "time.example", 1000);
  wifi.connected = true;
  ntp.synchronized = true;

  handler.update();

  TEST_ASSERT_TRUE(handler.isSynchronized());
  ntp.synchronized = false;
  handler.update();
  TEST_ASSERT_FALSE(handler.isSynchronized());
}

void test_returnsCurrentTimeWhenSynchronized()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);
  const time_t expectedTime = 1790000000;
  time_t actualTime = 0;
  wifi.connected = true;
  ntp.synchronized = true;
  ntp.unixTime = expectedTime;

  TEST_ASSERT_TRUE(handler.getCurrentTime(actualTime));
  TEST_ASSERT_EQUAL_INT32(expectedTime, actualTime);
}

void test_rejectsCurrentTimeWhenNotSynchronized()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);
  const time_t sentinel = 1234;
  time_t actualTime = sentinel;
  wifi.connected = true;

  TEST_ASSERT_FALSE(handler.getCurrentTime(actualTime));
  TEST_ASSERT_EQUAL_INT32(sentinel, actualTime);
}

void test_reportsSynchronizationOnlyWhenWifiAndNtpAreReady()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);

  TEST_ASSERT_FALSE(handler.isSynchronized());
  wifi.connected = true;
  TEST_ASSERT_FALSE(handler.isSynchronized());
  ntp.synchronized = true;
  TEST_ASSERT_TRUE(handler.isSynchronized());
}

void test_setsTimezoneAndReturnsLocalTime()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);
  struct tm expected = {};
  expected.tm_year = 126;
  expected.tm_mon = 8;
  expected.tm_mday = 12;
  expected.tm_hour = 1;
  expected.tm_min = 47;
  expected.tm_sec = 56;
  wifi.connected = true;
  ntp.synchronized = true;
  ntp.hasLocalTime = true;
  ntp.configuredLocalTime = expected;

  TEST_ASSERT_TRUE(handler.setTimezone("AWST-8"));
  TEST_ASSERT_EQUAL_STRING("AWST-8", handler.timezone());
  struct tm actual = {};
  TEST_ASSERT_TRUE(handler.getCurrentLocalTime(actual));
  TEST_ASSERT_EQUAL_INT(expected.tm_hour, actual.tm_hour);
  TEST_ASSERT_EQUAL_INT(expected.tm_min, actual.tm_min);
}

void test_rejectsLocalTimeWhenNotSynchronizedOrTimezoneInvalid()
{
  resetFixtures();
  NtpHandler handler(wifi, fakeClock, ntp);
  struct tm localTime = {};

  TEST_ASSERT_FALSE(handler.setTimezone(""));
  TEST_ASSERT_FALSE(handler.getCurrentLocalTime(localTime));
  wifi.connected = true;
  ntp.synchronized = true;
  TEST_ASSERT_FALSE(handler.getCurrentLocalTime(localTime));
}

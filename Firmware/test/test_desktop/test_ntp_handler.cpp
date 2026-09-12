#include <unity.h>

#include "NtpHandler.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeNtpClient.h"
#include "fakes/FakeWifiStation.h"

namespace {
FakeClock clock;
FakeWifiStation wifi;
FakeNtpClient ntp;

void resetFixtures()
{
  clock.currentMillis = 0;
  wifi.connected = false;
  ntp.reset();
}
}

void test_defaultsToPoolServerAndThirtyMinuteFrequency()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp);

  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", handler.server());
  TEST_ASSERT_EQUAL_UINT32(1800000UL, handler.updateFrequencyMs());
}

void test_requestsSynchronizationOnceWhenWifiFirstConnects()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp);

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
  NtpHandler handler(wifi, clock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  clock.advance(999);
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(1, ntp.requestCount);
  TEST_ASSERT_EQUAL_UINT32(2, ntp.updateCount);
}

void test_requestsAgainWhenFrequencyExpires()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  clock.advance(1000);
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(2, ntp.requestCount);
}

void test_reconnectionTriggersAnImmediateSynchronization()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp, "time.example", 1000);
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
  NtpHandler handler(wifi, clock, ntp, "time.example", 1000);
  wifi.connected = true;

  handler.update();
  handler.update();

  TEST_ASSERT_EQUAL_UINT32(2, ntp.updateCount);
}

void test_configurationCanBeChanged()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp);

  handler.setServer("custom.example");
  handler.setUpdateFrequencyMs(2500);

  TEST_ASSERT_EQUAL_STRING("custom.example", handler.server());
  TEST_ASSERT_EQUAL_UINT32(2500, handler.updateFrequencyMs());
}

void test_emptyServerIsRejected()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp);

  TEST_ASSERT_FALSE(handler.setServer(""));
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", handler.server());
}

void test_zeroFrequencyIsRejected()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp);

  TEST_ASSERT_FALSE(handler.setUpdateFrequencyMs(0));
  TEST_ASSERT_EQUAL_UINT32(1800000UL, handler.updateFrequencyMs());
}

void test_reportsSynchronizationState()
{
  resetFixtures();
  NtpHandler handler(wifi, clock, ntp, "time.example", 1000);
  wifi.connected = true;
  ntp.synchronized = true;

  handler.update();

  TEST_ASSERT_TRUE(handler.isSynchronized());
  ntp.synchronized = false;
  handler.update();
  TEST_ASSERT_FALSE(handler.isSynchronized());
}


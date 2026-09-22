#include <string>

#include <unity.h>

#include "MqttService.h"
#include "NtpService.h"
#include "WifiManager.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeMqttClient.h"
#include "fakes/FakeNtpAdapter.h"
#include "fakes/FakeWifi.h"

void testRuntimeLoadsPersistedConfiguration();
void testSerialStagesUntilApplyAndGatesOnPlugState();
void testRuntimeApplyFailureDoesNotChangeActiveConfiguration();
void testAppliedConfigurationIsOwnedFromLaterStagedEdits();
void testWifiManagerForwardsRssi();
void testNtpServiceExposesConfig();
void testStatusReporterWritesNothingBeforeBegin();
void testStatusReporterWritesNothingWhenUnplugged();
void testStatusReporterPrintsIdleStates();
void testStatusReporterPrintsConnectingWithoutRssi();
void testStatusReporterPrintsBackoffWithoutRssi();
void testStatusReporterPrintsConnectedRssi();
void testStatusReporterPrintsWaitingForSyncWithoutTime();
void testStatusReporterPrintsSynchronizedLocalTime();
void testStatusReporterAppliesDaylightOffset();
void testStatusReporterPrintsMqttWaitingForNetwork();
void testStatusReporterPrintsMqttConnected();
void testStatusReporterPrintsMqttBackoff();
void testStatusReporterUsesConfiguredInterval();
void testStatusReporterExposesConfig();
void testStatusReporterReconfigureChangesInterval();
void testStatusReporterWaitsForIntervalBeforeReprint();
void testStatusReporterAdvancesClockAfterSync();
void testStatusReporterStopsWhenUnpluggedAfterPrint();
void testStatusReporterPrintsEmptySlots();
void testStatusReporterSlotOneIsIndexZeroAddr10();
void testStatusReporterPrintsOnlineSlot();
void testStatusReporterPrintsFault();
void testSerialStatusDoesNotCallPingOrEcho();
void testCodecEncodesPingFrameWithCrc();
void testCrc8SmbusMatchesHeaderHelper();
void testCodecRejectsBadCrc();
void testCodecRejectsLengthUnderMinAndOverMax();
void testCodecDecodesIdentityBigEndian();
void testCodecRoundTripEchoPayload();
void testCodecParsesPaddedNineteenByteRead();
void testCodecParsesShortErrorInsidePaddedRead();
void testCodecIgnoresPadBytesAfterLength();
void testCodecEncodesSetAddressFourByteFrame();
void testHostBeginConfiguresSensePullupModInputCsPullup();
void testHostBeginCallsBusBeginAndAppliesClockAndTimeout();
void testUpdateDoesNothingBeforeBegin();
void testEmptySlotsStayEmpty();
void testPlugOneDoesNotEnumerateBeforeDebounce();
void testPlugOneWaitsBootWait();
void testPlugOneEnumeratesAfterDebounceAndBootWait();
void testWrapSafeDebounceTiming();
void testWrapSafeBootWaitTiming();
void testDebounceRestartsIfSenseBouncesHigh();
void testShortAbsenceIsGlitchFilterKeepsOnline();
void testStuckLowSenseWithNoAckGoesFaultNack();
void testFaultRetriesAfterFaultRetryMs();
void testFaultRetryWithNoAssignedAddressStillProbes0x0A();
void testCsUntouchedDuringEnumeration();
void testAssignsSlotDerivedAddress();
void testModOpenDrainLowOnlyDuringSelect();
void testModSettleElapsedBeforeFirstDefaultPing();
void testIdentifyOnlineIdentityEcho();
void testProtocolVersionNotOneIsUnsupported();
void testPingSucceedsWhenOnline();
void testEchoZeroAndSixteenByteRoundTrip();
void testEchoSeventeenBytesRejectedWithoutBus();
void testUnknownTypeIsUnsupportedNotCrash();
void testNackDuringIdentifyRetriesThenFault();
void testBadCrcDuringIdentifyIsFault();
void testBusyDuringIdentifyRetriesThenFaultBusy();
void testGetIdentityOkWithWrongLengthIsBadFrameNotUnsupported();
void testSetAddressWriteIsFourBytesWithStop();
void testSetAddressCommitSurvivesModRelease();
void testEnumLockHeldUntilIdentifyCompletes();
void testEnumLockReleasedOnFaultSoSecondSlotEnumerates();
void testEnumLockReleasedOnUnsupported();
void testUnplugMidEnumerateReleasesModAndLock();
void testUnplugAfterOnlineFreesAddressAndReturnsEmpty();
void testUnplugDuringBootWaitNeverTouchesBus();
void testReplugReenumerates();
void testAddressReuseAfterUnplug();
void testModuleMcuResetWithoutUnplugReenumerates();
void testHealthFailWhenStillAtAssignedAddressRecoversWithoutUnplug();
void testHealthPingBusyDoesNotIncrementFailCountStaysOnline();
void testIdentifyFailAfterAssignRecoversAtAssignedAddress();
void testVerifyFailWhenModuleCommittedRetriesAssignedNotOnlyDefault();
void testStuckSdaRecoversBusForOtherSlots();
void testStuckSdaStillStuckAfterRecoverStaysFault();
void testTwoSlotsInsertedTogetherDoNotCollide();
void testFourSlotsInsertedTogetherGetUniqueAddresses();
void testOneI2cTransactionPerUpdateGlobally();
void testHealthPingDoesNotShareUpdateWithEnumTxn();
void testWriteReadNackOnWriteDoesNotParseRx();
void testNoncompliantAlwaysAck0x0AIsDetected();
void testSenseGlitchDuringWaitForLockDoesNotAbortEnumerate();
void testSetAddressNackRetries();

namespace
{
FakeClock* clockForCallback = nullptr;
std::string receivedTopic;
std::string receivedPayload;

void recordMessage(const char* topic, const uint8_t* payload, size_t length,
                   void*)
{
  receivedTopic = topic == nullptr ? "" : topic;
  receivedPayload.assign(reinterpret_cast<const char*>(payload), length);
}

WifiManagerConfig wifiConfig()
{
  return {"garden", "secret", 1000, 100, 400};
}

MqttConfig mqttConfig()
{
  static const MqttSubscription subscriptions[] = {
    {"controller/command", 1},
    {"controller/config", 0},
  };
  return {"watering-controller", "user", "password", subscriptions, 2, 100, 400};
}
}

void setUp()
{
  clockForCallback = nullptr;
  receivedTopic.clear();
  receivedPayload.clear();
}

void tearDown()
{
}

void testWifiStartsConnectingAndReachesConnectedState()
{
  FakeClock clock;
  FakeWifi wifi;
  WifiManager manager(wifi, clock, wifiConfig());

  manager.begin();

  TEST_ASSERT_EQUAL(WifiManagerState::Connecting, manager.state());
  TEST_ASSERT_EQUAL(1, wifi.beginCallCount);
  TEST_ASSERT_EQUAL_STRING("garden", wifi.lastSsid.c_str());

  wifi.linkState = WifiLinkState::Connected;
  manager.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Connected, manager.state());
  TEST_ASSERT_TRUE(manager.isConnected());
}

void testServicesRemainIdleUntilStarted()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeNtpAdapter ntpAdapter;
  FakeMqttClient mqttClient;
  WifiManager wifiManager(wifi, clock, wifiConfig());
  NtpService ntpService(ntpAdapter, clock,
                        {"a", "b", "c", 0, 0, 100});
  MqttService mqttService(mqttClient, clock, mqttConfig());

  wifiManager.update();
  ntpService.update();
  mqttService.update(true);

  TEST_ASSERT_EQUAL(WifiManagerState::Idle, wifiManager.state());
  TEST_ASSERT_EQUAL(NtpServiceState::Idle, ntpService.state());
  TEST_ASSERT_EQUAL(MqttServiceState::Idle, mqttService.state());
  TEST_ASSERT_EQUAL(0, wifi.beginCallCount);
  TEST_ASSERT_EQUAL(0, ntpAdapter.configureCallCount);
  TEST_ASSERT_EQUAL(0, mqttClient.connectCallCount);
}

void testWifiTimeoutUsesBackoffWithoutBlocking()
{
  FakeClock clock;
  FakeWifi wifi;
  WifiManager manager(wifi, clock, wifiConfig());

  manager.begin();
  clock.advance(1000);
  manager.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, manager.state());
  TEST_ASSERT_EQUAL(1, wifi.disconnectCallCount);
  TEST_ASSERT_EQUAL(1, wifi.beginCallCount);

  clock.advance(99);
  manager.update();
  TEST_ASSERT_EQUAL(1, wifi.beginCallCount);

  clock.advance(1);
  manager.update();
  TEST_ASSERT_EQUAL(WifiManagerState::Connecting, manager.state());
  TEST_ASSERT_EQUAL(2, wifi.beginCallCount);
}

void testWifiRetriesWithExponentialDelayAndCapsDelay()
{
  FakeClock clock;
  FakeWifi wifi;
  WifiManager manager(wifi, clock, wifiConfig());

  manager.begin();
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    clock.advance(1000);
    manager.update();
    TEST_ASSERT_EQUAL(WifiManagerState::Backoff, manager.state());
    clock.advance(attempt == 0 ? 100 : (attempt == 1 ? 200 : 400));
    manager.update();
  }

  TEST_ASSERT_EQUAL(4, wifi.beginCallCount);
  TEST_ASSERT_EQUAL(400, manager.currentRetryDelayMs());
}

void testWifiReturnsToBackoffWhenConnectedLinkIsLost()
{
  FakeClock clock;
  FakeWifi wifi;
  WifiManager manager(wifi, clock, wifiConfig());

  manager.begin();
  wifi.linkState = WifiLinkState::Connected;
  manager.update();
  wifi.linkState = WifiLinkState::Disconnected;
  manager.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, manager.state());
  TEST_ASSERT_FALSE(manager.isConnected());
}

void testWifiUsesStatusSequenceAndHandlesClockWraparound()
{
  FakeClock clock;
  clock.set(UINT32_MAX - 50U);
  FakeWifi wifi;
  wifi.statusSequence = {
    WifiLinkState::Connecting,
    WifiLinkState::Connected,
  };
  WifiManager manager(wifi, clock, wifiConfig());

  manager.begin();
  clock.advance(1000);
  manager.update();

  TEST_ASSERT_EQUAL(WifiManagerState::Backoff, manager.state());
  TEST_ASSERT_EQUAL(1, wifi.disconnectCallCount);

  clock.advance(101);
  manager.update();
  TEST_ASSERT_EQUAL(WifiManagerState::Connecting, manager.state());
  TEST_ASSERT_EQUAL(2, wifi.beginCallCount);

  manager.update();
  TEST_ASSERT_EQUAL(WifiManagerState::Connected, manager.state());
}

void testNtpConfiguresAdapterAndTransitionsWhenTimeIsValid()
{
  FakeClock clock;
  FakeNtpAdapter adapter;
  NtpService service(adapter, clock,
                     {"pool.ntp.org", "time.nist.gov", nullptr, 28800, 0, 500});

  service.begin();
  TEST_ASSERT_EQUAL(NtpServiceState::WaitingForSync, service.state());
  TEST_ASSERT_FALSE(service.isSynchronized());
  TEST_ASSERT_EQUAL((time_t)0, service.currentTime());
  TEST_ASSERT_EQUAL(1, adapter.configureCallCount);
  TEST_ASSERT_EQUAL(28800, adapter.lastUtcOffsetSeconds);

  service.update();
  TEST_ASSERT_EQUAL(NtpServiceState::WaitingForSync, service.state());

  adapter.synchronized = true;
  adapter.epoch = 1700000000;
  service.update();
  TEST_ASSERT_EQUAL(NtpServiceState::Synchronized, service.state());
  TEST_ASSERT_EQUAL((time_t)1700000000, service.currentTime());
}

void testNtpRetriesConfigurationAfterSyncWaitInterval()
{
  FakeClock clock;
  FakeNtpAdapter adapter;
  NtpService service(adapter, clock,
                     {"a", "b", "c", 0, 3600, 500});

  service.begin();
  clock.advance(499);
  service.update();
  TEST_ASSERT_EQUAL(1, adapter.configureCallCount);

  clock.advance(1);
  service.update();
  TEST_ASSERT_EQUAL(2, adapter.configureCallCount);
}

void testNtpReturnsToWaitingWhenSynchronizationIsLost()
{
  FakeClock clock;
  FakeNtpAdapter adapter;
  NtpService service(adapter, clock,
                     {"a", "b", "c", 0, 0, 100});

  service.begin();
  adapter.synchronized = true;
  adapter.epoch = 1700000001;
  service.update();
  TEST_ASSERT_EQUAL(NtpServiceState::Synchronized, service.state());

  adapter.synchronized = false;
  service.update();
  TEST_ASSERT_EQUAL(NtpServiceState::WaitingForSync, service.state());
  TEST_ASSERT_EQUAL((time_t)0, service.currentTime());

  clock.advance(100);
  service.update();
  TEST_ASSERT_EQUAL(2, adapter.configureCallCount);
}

void testMqttWaitsForNetworkThenConnectsAndSubscribes()
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(false);
  TEST_ASSERT_EQUAL(MqttServiceState::WaitingForNetwork, service.state());
  TEST_ASSERT_EQUAL(0, client.connectCallCount);

  service.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Connected, service.state());
  TEST_ASSERT_EQUAL(1, client.connectCallCount);
  TEST_ASSERT_EQUAL(2, client.subscribeCallCount);
  TEST_ASSERT_EQUAL_STRING("controller/command",
                           client.subscribedTopics[0].c_str());
}

void testMqttFailedConnectionUsesNonblockingBackoff()
{
  FakeClock clock;
  FakeMqttClient client;
  client.connectResult = false;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Backoff, service.state());
  TEST_ASSERT_EQUAL(1, client.connectCallCount);

  clock.advance(99);
  service.update(true);
  TEST_ASSERT_EQUAL(1, client.connectCallCount);

  clock.advance(1);
  service.update(true);
  TEST_ASSERT_EQUAL(2, client.connectCallCount);
}

void testMqttServicesLoopAndPublishesOnlyWhenConnected()
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  TEST_ASSERT_FALSE(service.publish("status", "offline", false));
  service.update(true);
  TEST_ASSERT_TRUE(service.publish("status", "online", true));
  service.update(true);

  TEST_ASSERT_EQUAL(1, client.publishCallCount);
  TEST_ASSERT_EQUAL_STRING("online", client.publishedMessages[0].payload.c_str());
  TEST_ASSERT_EQUAL(1, client.loopCallCount);
}

void testMqttForwardsInboundMessagesToApplicationHandler()
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService service(client, clock, mqttConfig());

  service.setMessageHandler(recordMessage, nullptr);
  service.begin();
  service.update(true);
  client.deliver("controller/command", "water-now");

  TEST_ASSERT_EQUAL_STRING("controller/command", receivedTopic.c_str());
  TEST_ASSERT_EQUAL_STRING("water-now", receivedPayload.c_str());
}

void testMqttDisconnectsWhenNetworkIsLost()
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(true);
  service.update(false);

  TEST_ASSERT_EQUAL(MqttServiceState::WaitingForNetwork, service.state());
  TEST_ASSERT_EQUAL(1, client.disconnectCallCount);
}

void testMqttRetriesAfterBrokerDisconnectUsingBackoff()
{
  FakeClock clock;
  FakeMqttClient client;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Connected, service.state());

  client.connectedState = false;
  service.update(true);
  TEST_ASSERT_EQUAL(MqttServiceState::Backoff, service.state());

  clock.advance(99);
  service.update(true);
  TEST_ASSERT_EQUAL(1, client.connectCallCount);

  clock.advance(1);
  service.update(true);
  TEST_ASSERT_EQUAL(2, client.connectCallCount);
  TEST_ASSERT_EQUAL(MqttServiceState::Connected, service.state());
}

void testMqttReportsPublicationFailure()
{
  FakeClock clock;
  FakeMqttClient client;
  client.publishResult = false;
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(true);

  TEST_ASSERT_FALSE(service.publish("status", "online", false));
  TEST_ASSERT_EQUAL(1, client.publishCallCount);
}

void testMqttBacksOffWhenSubscriptionFails()
{
  FakeClock clock;
  FakeMqttClient client;
  client.subscribeResults = {true, false};
  MqttService service(client, clock, mqttConfig());

  service.begin();
  service.update(true);

  TEST_ASSERT_EQUAL(MqttServiceState::Backoff, service.state());
  TEST_ASSERT_EQUAL(1, client.disconnectCallCount);
  TEST_ASSERT_FALSE(service.publish("status", "online", false));
}

void testWifiAndMqttComposeThroughInterfaces()
{
  FakeClock clock;
  FakeWifi wifi;
  FakeMqttClient client;
  WifiManager manager(wifi, clock, wifiConfig());
  MqttService mqtt(client, clock, mqttConfig());

  manager.begin();
  mqtt.begin();
  mqtt.update(manager.isConnected());
  TEST_ASSERT_EQUAL(0, client.connectCallCount);

  wifi.linkState = WifiLinkState::Connected;
  manager.update();
  mqtt.update(manager.isConnected());

  TEST_ASSERT_EQUAL(1, client.connectCallCount);
  TEST_ASSERT_EQUAL(MqttServiceState::Connected, mqtt.state());
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(testWifiStartsConnectingAndReachesConnectedState);
  RUN_TEST(testServicesRemainIdleUntilStarted);
  RUN_TEST(testWifiTimeoutUsesBackoffWithoutBlocking);
  RUN_TEST(testWifiRetriesWithExponentialDelayAndCapsDelay);
  RUN_TEST(testWifiReturnsToBackoffWhenConnectedLinkIsLost);
  RUN_TEST(testWifiUsesStatusSequenceAndHandlesClockWraparound);
  RUN_TEST(testNtpConfiguresAdapterAndTransitionsWhenTimeIsValid);
  RUN_TEST(testNtpRetriesConfigurationAfterSyncWaitInterval);
  RUN_TEST(testNtpReturnsToWaitingWhenSynchronizationIsLost);
  RUN_TEST(testMqttWaitsForNetworkThenConnectsAndSubscribes);
  RUN_TEST(testMqttFailedConnectionUsesNonblockingBackoff);
  RUN_TEST(testMqttServicesLoopAndPublishesOnlyWhenConnected);
  RUN_TEST(testMqttForwardsInboundMessagesToApplicationHandler);
  RUN_TEST(testMqttDisconnectsWhenNetworkIsLost);
  RUN_TEST(testMqttRetriesAfterBrokerDisconnectUsingBackoff);
  RUN_TEST(testMqttReportsPublicationFailure);
  RUN_TEST(testMqttBacksOffWhenSubscriptionFails);
  RUN_TEST(testWifiAndMqttComposeThroughInterfaces);
  RUN_TEST(testRuntimeLoadsPersistedConfiguration);
  RUN_TEST(testSerialStagesUntilApplyAndGatesOnPlugState);
  RUN_TEST(testRuntimeApplyFailureDoesNotChangeActiveConfiguration);
  RUN_TEST(testAppliedConfigurationIsOwnedFromLaterStagedEdits);
  RUN_TEST(testWifiManagerForwardsRssi);
  RUN_TEST(testNtpServiceExposesConfig);
  RUN_TEST(testStatusReporterWritesNothingBeforeBegin);
  RUN_TEST(testStatusReporterWritesNothingWhenUnplugged);
  RUN_TEST(testStatusReporterPrintsIdleStates);
  RUN_TEST(testStatusReporterPrintsConnectingWithoutRssi);
  RUN_TEST(testStatusReporterPrintsBackoffWithoutRssi);
  RUN_TEST(testStatusReporterPrintsConnectedRssi);
  RUN_TEST(testStatusReporterPrintsWaitingForSyncWithoutTime);
  RUN_TEST(testStatusReporterPrintsSynchronizedLocalTime);
  RUN_TEST(testStatusReporterAppliesDaylightOffset);
  RUN_TEST(testStatusReporterPrintsMqttWaitingForNetwork);
  RUN_TEST(testStatusReporterPrintsMqttConnected);
  RUN_TEST(testStatusReporterPrintsMqttBackoff);
  RUN_TEST(testStatusReporterUsesConfiguredInterval);
  RUN_TEST(testStatusReporterExposesConfig);
  RUN_TEST(testStatusReporterReconfigureChangesInterval);
  RUN_TEST(testStatusReporterWaitsForIntervalBeforeReprint);
  RUN_TEST(testStatusReporterAdvancesClockAfterSync);
  RUN_TEST(testStatusReporterStopsWhenUnpluggedAfterPrint);
  RUN_TEST(testStatusReporterPrintsEmptySlots);
  RUN_TEST(testStatusReporterSlotOneIsIndexZeroAddr10);
  RUN_TEST(testStatusReporterPrintsOnlineSlot);
  RUN_TEST(testStatusReporterPrintsFault);
  RUN_TEST(testSerialStatusDoesNotCallPingOrEcho);
  RUN_TEST(testCodecEncodesPingFrameWithCrc);
  RUN_TEST(testCrc8SmbusMatchesHeaderHelper);
  RUN_TEST(testCodecRejectsBadCrc);
  RUN_TEST(testCodecRejectsLengthUnderMinAndOverMax);
  RUN_TEST(testCodecDecodesIdentityBigEndian);
  RUN_TEST(testCodecRoundTripEchoPayload);
  RUN_TEST(testCodecParsesPaddedNineteenByteRead);
  RUN_TEST(testCodecParsesShortErrorInsidePaddedRead);
  RUN_TEST(testCodecIgnoresPadBytesAfterLength);
  RUN_TEST(testCodecEncodesSetAddressFourByteFrame);
  RUN_TEST(testHostBeginConfiguresSensePullupModInputCsPullup);
  RUN_TEST(testHostBeginCallsBusBeginAndAppliesClockAndTimeout);
  RUN_TEST(testUpdateDoesNothingBeforeBegin);
  RUN_TEST(testEmptySlotsStayEmpty);
  RUN_TEST(testPlugOneDoesNotEnumerateBeforeDebounce);
  RUN_TEST(testPlugOneWaitsBootWait);
  RUN_TEST(testPlugOneEnumeratesAfterDebounceAndBootWait);
  RUN_TEST(testWrapSafeDebounceTiming);
  RUN_TEST(testWrapSafeBootWaitTiming);
  RUN_TEST(testDebounceRestartsIfSenseBouncesHigh);
  RUN_TEST(testShortAbsenceIsGlitchFilterKeepsOnline);
  RUN_TEST(testStuckLowSenseWithNoAckGoesFaultNack);
  RUN_TEST(testFaultRetriesAfterFaultRetryMs);
  RUN_TEST(testFaultRetryWithNoAssignedAddressStillProbes0x0A);
  RUN_TEST(testCsUntouchedDuringEnumeration);
  RUN_TEST(testAssignsSlotDerivedAddress);
  RUN_TEST(testModOpenDrainLowOnlyDuringSelect);
  RUN_TEST(testModSettleElapsedBeforeFirstDefaultPing);
  RUN_TEST(testIdentifyOnlineIdentityEcho);
  RUN_TEST(testProtocolVersionNotOneIsUnsupported);
  RUN_TEST(testPingSucceedsWhenOnline);
  RUN_TEST(testEchoZeroAndSixteenByteRoundTrip);
  RUN_TEST(testEchoSeventeenBytesRejectedWithoutBus);
  RUN_TEST(testUnknownTypeIsUnsupportedNotCrash);
  RUN_TEST(testNackDuringIdentifyRetriesThenFault);
  RUN_TEST(testBadCrcDuringIdentifyIsFault);
  RUN_TEST(testBusyDuringIdentifyRetriesThenFaultBusy);
  RUN_TEST(testGetIdentityOkWithWrongLengthIsBadFrameNotUnsupported);
  RUN_TEST(testSetAddressWriteIsFourBytesWithStop);
  RUN_TEST(testSetAddressCommitSurvivesModRelease);
  RUN_TEST(testEnumLockHeldUntilIdentifyCompletes);
  RUN_TEST(testEnumLockReleasedOnFaultSoSecondSlotEnumerates);
  RUN_TEST(testEnumLockReleasedOnUnsupported);
  RUN_TEST(testUnplugMidEnumerateReleasesModAndLock);
  RUN_TEST(testUnplugAfterOnlineFreesAddressAndReturnsEmpty);
  RUN_TEST(testUnplugDuringBootWaitNeverTouchesBus);
  RUN_TEST(testReplugReenumerates);
  RUN_TEST(testAddressReuseAfterUnplug);
  RUN_TEST(testModuleMcuResetWithoutUnplugReenumerates);
  RUN_TEST(testHealthFailWhenStillAtAssignedAddressRecoversWithoutUnplug);
  RUN_TEST(testHealthPingBusyDoesNotIncrementFailCountStaysOnline);
  RUN_TEST(testIdentifyFailAfterAssignRecoversAtAssignedAddress);
  RUN_TEST(testVerifyFailWhenModuleCommittedRetriesAssignedNotOnlyDefault);
  RUN_TEST(testStuckSdaRecoversBusForOtherSlots);
  RUN_TEST(testStuckSdaStillStuckAfterRecoverStaysFault);
  RUN_TEST(testTwoSlotsInsertedTogetherDoNotCollide);
  RUN_TEST(testFourSlotsInsertedTogetherGetUniqueAddresses);
  RUN_TEST(testOneI2cTransactionPerUpdateGlobally);
  RUN_TEST(testHealthPingDoesNotShareUpdateWithEnumTxn);
  RUN_TEST(testWriteReadNackOnWriteDoesNotParseRx);
  RUN_TEST(testNoncompliantAlwaysAck0x0AIsDetected);
  RUN_TEST(testSenseGlitchDuringWaitForLockDoesNotAbortEnumerate);
  RUN_TEST(testSetAddressNackRetries);
  return UNITY_END();
}

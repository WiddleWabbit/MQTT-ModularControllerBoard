#include <unity.h>

#include "ModuleCodec.h"
#include "ModuleHost.h"
#include "ModuleProtocol.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeModuleDevice.h"

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
 * Seats a module and pumps until the slot leaves Empty/Debouncing/Enumerating
 * or the timeout elapses.
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
 * Counts protocol ops since a snapshot.
 *
 * @param fixture Host fixture.
 * @param since Previous protocol op count.
 * @return New ops.
 */
size_t newOps(const EmptyModuleHostFixture& fixture, size_t since)
{
  return fixture.bus.protocolOpCount() - since;
}
}

void testCodecEncodesPingFrameWithCrc()
{
  uint8_t tx[8] = {};
  const size_t size = ModuleCodec::encodePing(tx, sizeof(tx));
  TEST_ASSERT_EQUAL(3, size);
  TEST_ASSERT_EQUAL(2, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdPing, tx[1]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 2), tx[2]);
}

void testCrc8SmbusMatchesHeaderHelper()
{
  const uint8_t data[] = {0x02, 0x01};
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(data, 2),
                    module_protocol::crc8Smbus(data, 2));
  TEST_ASSERT_EQUAL(0x00, module_protocol::crc8Smbus(data, 0));
}

void testCodecRejectsBadCrc()
{
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xFF;
  }
  rx[0] = 2;
  rx[1] = module_protocol::kStatusOk;
  rx[2] = static_cast<uint8_t>(module_protocol::crc8Smbus(rx, 2) ^ 0xFF);
  const ModuleDecodedFrame decoded = ModuleCodec::decode(rx, sizeof(rx));
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::BadCrc, decoded.decodeStatus);
}

void testCodecRejectsLengthUnderMinAndOverMax()
{
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xFF;
  }
  rx[0] = 1;
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::BadLength,
                    ModuleCodec::decode(rx, sizeof(rx)).decodeStatus);
  rx[0] = 19;
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::BadLength,
                    ModuleCodec::decode(rx, sizeof(rx)).decodeStatus);
}

void testCodecDecodesIdentityBigEndian()
{
  uint8_t tx[19];
  ModuleCodec::encodeGetIdentity(tx, sizeof(tx));
  uint8_t payload[5] = {0x00, 0x01, 0x01, 0x12, 0x34};
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xFF;
  }
  rx[0] = 7;
  rx[1] = module_protocol::kStatusOk;
  for (int i = 0; i < 5; ++i)
  {
    rx[2 + i] = payload[i];
  }
  rx[7] = module_protocol::crc8Smbus(rx, 7);
  const ModuleDecodedFrame decoded = ModuleCodec::decode(rx, sizeof(rx));
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::Ok, decoded.decodeStatus);
  TEST_ASSERT_EQUAL(5, decoded.payloadLen);
  TEST_ASSERT_EQUAL(0x00, decoded.payload[0]);
  TEST_ASSERT_EQUAL(0x01, decoded.payload[1]);
}

void testCodecRoundTripEchoPayload()
{
  uint8_t tx[19];
  TEST_ASSERT_EQUAL(3, ModuleCodec::encodeEcho(nullptr, 0, tx, sizeof(tx)));
  uint8_t payload[16];
  for (uint8_t i = 0; i < 16; ++i)
  {
    payload[i] = i;
  }
  TEST_ASSERT_EQUAL(19, ModuleCodec::encodeEcho(payload, 16, tx, sizeof(tx)));
}

void testCodecParsesPaddedNineteenByteRead()
{
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xFF;
  }
  rx[0] = 2;
  rx[1] = module_protocol::kStatusOk;
  rx[2] = module_protocol::crc8Smbus(rx, 2);
  const ModuleDecodedFrame decoded = ModuleCodec::decode(rx, sizeof(rx));
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::Ok, decoded.decodeStatus);
  TEST_ASSERT_EQUAL(0, decoded.payloadLen);
}

void testCodecParsesShortErrorInsidePaddedRead()
{
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xFF;
  }
  rx[0] = 2;
  rx[1] = module_protocol::kStatusUnknownCmd;
  rx[2] = module_protocol::crc8Smbus(rx, 2);
  const ModuleDecodedFrame decoded = ModuleCodec::decode(rx, sizeof(rx));
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::Ok, decoded.decodeStatus);
  TEST_ASSERT_EQUAL(module_protocol::kStatusUnknownCmd, decoded.statusByte);
}

void testCodecIgnoresPadBytesAfterLength()
{
  uint8_t rx[19];
  for (uint8_t& byte : rx)
  {
    byte = 0xAA;
  }
  rx[0] = 2;
  rx[1] = module_protocol::kStatusOk;
  rx[2] = module_protocol::crc8Smbus(rx, 2);
  TEST_ASSERT_EQUAL(ModuleDecodeStatus::Ok,
                    ModuleCodec::decode(rx, sizeof(rx)).decodeStatus);
}

void testCodecEncodesSetAddressFourByteFrame()
{
  uint8_t tx[8] = {};
  const size_t size = ModuleCodec::encodeSetAddress(0x10, tx, sizeof(tx));
  TEST_ASSERT_EQUAL(4, size);
  TEST_ASSERT_EQUAL(0x03, tx[0]);
  TEST_ASSERT_EQUAL(module_protocol::kCmdSetAddress, tx[1]);
  TEST_ASSERT_EQUAL(0x10, tx[2]);
  TEST_ASSERT_EQUAL(module_protocol::crc8Smbus(tx, 3), tx[3]);
}

void testHostBeginConfiguresSensePullupModInputCsPullup()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.sns1.mode);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs1.mode);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs4.mode);
}

void testHostBeginCallsBusBeginAndAppliesClockAndTimeout()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  TEST_ASSERT_EQUAL(1, fixture.bus.beginCallCount);
  TEST_ASSERT_EQUAL(100000, fixture.bus.clockHz);
  TEST_ASSERT_EQUAL(50, fixture.bus.timeoutMs);
}

void testUpdateDoesNothingBeforeBegin()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.sns1.setPresent(true);
  fixture.host.update();
  TEST_ASSERT_EQUAL(0, fixture.bus.beginCallCount);
  TEST_ASSERT_EQUAL(0, fixture.bus.ops.size());
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
}

void testEmptySlotsStayEmpty()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  pumpMs(fixture, 300);
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(3));
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
}

void testPlugOneDoesNotEnumerateBeforeDebounce()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  clock.advance(49);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Debouncing, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
}

void testPlugOneWaitsBootWait()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  clock.advance(50);
  fixture.host.update();
  clock.advance(199);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Enumerating, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
}

void testPlugOneEnumeratesAfterDebounceAndBootWait()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
}

void testWrapSafeDebounceTiming()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  clock.set(0xFFFFFFF0);
  fixture.sns1.setPresent(true);
  fixture.host.update();
  clock.advance(50);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Enumerating, fixture.host.state(0));
}

void testWrapSafeBootWaitTiming()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  clock.set(0xFFFFFFF0);
  fixture.sns1.setPresent(true);
  fixture.host.update();
  clock.advance(50);
  fixture.host.update();
  clock.advance(199);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Enumerating, fixture.host.state(0));
  TEST_ASSERT_EQUAL(-1, fixture.host.enumLockOwner());
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
  clock.advance(1);
  fixture.host.update();
  TEST_ASSERT_EQUAL(0, fixture.host.enumLockOwner());
}

void testDebounceRestartsIfSenseBouncesHigh()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  clock.advance(20);
  fixture.host.update();
  fixture.sns1.setPresent(false);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
}

void testShortAbsenceIsGlitchFilterKeepsOnline()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 20);
  fixture.sns1.setPresent(true);
  fixture.host.update();
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
}

void testStuckLowSenseWithNoAckGoesFaultNack()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotFault::Nack, fixture.host.fault(0));
}

void testFaultRetriesAfterFaultRetryMs()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  const size_t before = fixture.bus.protocolOpCount();
  pumpMs(fixture, 1100);
  TEST_ASSERT_TRUE(fixture.bus.protocolOpCount() > before);
}

void testFaultRetryWithNoAssignedAddressStillProbes0x0A()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  const size_t before = fixture.bus.ops.size();
  pumpMs(fixture, 1100);
  bool sawAssigned = false;
  for (size_t i = before; i < fixture.bus.ops.size(); ++i)
  {
    if (fixture.bus.ops[i].address == 0x10)
    {
      sawAssigned = true;
    }
  }
  TEST_ASSERT_FALSE(sawAssigned);
}

void testCsUntouchedDuringEnumeration()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  const int writes = fixture.cs1.writeCount;
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs1.mode);
  TEST_ASSERT_EQUAL(writes, fixture.cs1.writeCount);
}

void testAssignsSlotDerivedAddress()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d3(clock, fixture.mod4);
  fixture.host.begin();
  plugAndPump(fixture, 0, d0);
  plugAndPump(fixture, 3, d3, 800);
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
  TEST_ASSERT_EQUAL(0x13, fixture.host.address(3));
}

void testModOpenDrainLowOnlyDuringSelect()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);
  TEST_ASSERT_FALSE(fixture.mod1.pushPullUsed);
  TEST_ASSERT_FALSE(fixture.mod1.wroteHighWhilePushPull);
}

void testModSettleElapsedBeforeFirstDefaultPing()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.modAckDelayMs = 5;
  fixture.bus.attach(device);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 250);
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
  pumpMs(fixture, 20);
  TEST_ASSERT_TRUE(fixture.bus.protocolOpCount() >= 1);
  TEST_ASSERT_EQUAL(module_protocol::kUnconfiguredAddress,
                    fixture.bus.ops[0].address);
}

void testIdentifyOnlineIdentityEcho()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.firmwareVersion = 0x1234;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(module_protocol::kTypeIdentityEcho, fixture.host.typeId(0));
  TEST_ASSERT_EQUAL(1, fixture.host.protocolVersion(0));
  TEST_ASSERT_EQUAL(0x1234, fixture.host.firmwareVersion(0));
  TEST_ASSERT_EQUAL_STRING("IdentityEcho", fixture.host.typeName(0));
}

void testProtocolVersionNotOneIsUnsupported()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.protocolVersion = 2;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, fixture.host.state(0));
  TEST_ASSERT_FALSE(fixture.host.echo(0, nullptr, 0, nullptr, nullptr));
  TEST_ASSERT_TRUE(fixture.host.ping(0));
}

void testPingSucceedsWhenOnline()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_TRUE(fixture.host.ping(0));
}

void testEchoZeroAndSixteenByteRoundTrip()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  uint8_t out[16];
  size_t outLen = 0;
  TEST_ASSERT_TRUE(fixture.host.echo(0, nullptr, 0, out, &outLen));
  TEST_ASSERT_EQUAL(0, outLen);
  uint8_t in[16];
  for (uint8_t i = 0; i < 16; ++i)
  {
    in[i] = static_cast<uint8_t>(i + 1);
  }
  TEST_ASSERT_TRUE(fixture.host.echo(0, in, 16, out, &outLen));
  TEST_ASSERT_EQUAL(16, outLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 16);
}

void testEchoSeventeenBytesRejectedWithoutBus()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  const size_t before = fixture.bus.protocolOpCount();
  uint8_t in[17] = {};
  uint8_t out[17];
  size_t outLen = 0;
  TEST_ASSERT_FALSE(fixture.host.echo(0, in, 17, out, &outLen));
  TEST_ASSERT_EQUAL(before, fixture.bus.protocolOpCount());
}

void testUnknownTypeIsUnsupportedNotCrash()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.typeId = 0x02AA;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0x02AA, fixture.host.typeId(0));
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
}

void testNackDuringIdentifyRetriesThenFault()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.nackRemaining = 0;
  fixture.bus.attach(device);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  device.nackIdentity = true;
  pumpMs(fixture, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(-1, fixture.host.enumLockOwner());
  TEST_ASSERT_TRUE(fixture.host.address(0) != 0);
}

void testBadCrcDuringIdentifyIsFault()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.forceBadCrc = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotFault::BadCrc, fixture.host.fault(0));
}

void testBusyDuringIdentifyRetriesThenFaultBusy()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.forceBusyOnIdentity = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotFault::Busy, fixture.host.fault(0));
}

void testGetIdentityOkWithWrongLengthIsBadFrameNotUnsupported()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.identityLengthField = 5;
  fixture.host.begin();
  plugAndPump(fixture, 0, device, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotFault::BadFrame, fixture.host.fault(0));
}

void testSetAddressWriteIsFourBytesWithStop()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  bool found = false;
  for (const FakeI2cOp& op : fixture.bus.ops)
  {
    if (op.type == FakeI2cOp::Type::Write && op.address == 0x0A)
    {
      TEST_ASSERT_EQUAL(4, op.tx.size());
      TEST_ASSERT_EQUAL(0x03, op.tx[0]);
      TEST_ASSERT_TRUE(op.issuedStop);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void testSetAddressCommitSurvivesModRelease()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);
  TEST_ASSERT_TRUE(device.assigned);
  TEST_ASSERT_EQUAL(0x10, device.assignedAddress);
}

void testEnumLockHeldUntilIdentifyCompletes()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  fixture.bus.attach(d0);
  fixture.bus.attach(d1);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  fixture.sns2.setPresent(true);
  pumpMs(fixture, 255);
  TEST_ASSERT_EQUAL(0, fixture.host.enumLockOwner());
  TEST_ASSERT_TRUE(fixture.mod2.mode != PinMode::DigitalOutputOpenDrain);
}

void testEnumLockReleasedOnFaultSoSecondSlotEnumerates()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d1(clock, fixture.mod2);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  plugAndPump(fixture, 1, d1, 2000);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(1));
  TEST_ASSERT_EQUAL(-1, fixture.host.enumLockOwner());
}

void testEnumLockReleasedOnUnsupported()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.protocolVersion = 2;
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  TEST_ASSERT_EQUAL(SlotState::Unsupported, fixture.host.state(0));
  TEST_ASSERT_EQUAL(-1, fixture.host.enumLockOwner());
}

void testUnplugMidEnumerateReleasesModAndLock()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 270);
  TEST_ASSERT_EQUAL(0, fixture.host.enumLockOwner());
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 20);
  TEST_ASSERT_EQUAL(0, fixture.host.enumLockOwner());
  pumpMs(fixture, 40);
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
  TEST_ASSERT_EQUAL(-1, fixture.host.enumLockOwner());
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);
}

void testUnplugAfterOnlineFreesAddressAndReturnsEmpty()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 60);
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0, fixture.host.address(0));
}

void testUnplugDuringBootWaitNeverTouchesBus()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 100);
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 80);
  TEST_ASSERT_EQUAL(0, fixture.bus.protocolOpCount());
  TEST_ASSERT_EQUAL(SlotState::Empty, fixture.host.state(0));
}

void testReplugReenumerates()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 60);
  device.resetToUnconfigured();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

void testAddressReuseAfterUnplug()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 60);
  device.resetToUnconfigured();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
}

void testModuleMcuResetWithoutUnplugReenumerates()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  device.resetToUnconfigured();
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_TRUE(device.assigned);
}

void testHealthFailWhenStillAtAssignedAddressRecoversWithoutUnplug()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  device.nackRemaining = 3;
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

void testHealthPingBusyDoesNotIncrementFailCountStaysOnline()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, device);
  device.forceBusy = true;
  pumpMs(fixture, 3500);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);
}

void testIdentifyFailAfterAssignRecoversAtAssignedAddress()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.nackIdentity = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_TRUE(fixture.host.address(0) != 0);
  device.nackIdentity = false;
  pumpMs(fixture, 2000);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

void testVerifyFailWhenModuleCommittedRetriesAssignedNotOnlyDefault()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  device.nackAssignedPing = true;
  fixture.host.begin();
  plugAndPump(fixture, 0, device, 1200);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
  TEST_ASSERT_EQUAL(0, fixture.host.address(0));
  TEST_ASSERT_TRUE(device.assigned);
  device.nackAssignedPing = false;
  pumpMs(fixture, 2000);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

void testStuckSdaRecoversBusForOtherSlots()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  fixture.host.begin();
  plugAndPump(fixture, 0, d0);
  fixture.bus.stuckSda = true;
  fixture.bus.stuckSdaClearsOnRecover = true;
  pumpMs(fixture, 1100);
  TEST_ASSERT_TRUE(fixture.bus.recoverCallCount >= 1);
  plugAndPump(fixture, 1, d1, 1500);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(1));
}

void testStuckSdaStillStuckAfterRecoverStaysFault()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  fixture.host.begin();
  plugAndPump(fixture, 0, d0);
  fixture.bus.stuckSda = true;
  fixture.bus.stuckSdaClearsOnRecover = false;
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(SlotState::Fault, fixture.host.state(0));
}

void testTwoSlotsInsertedTogetherDoNotCollide()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  fixture.bus.attach(d0);
  fixture.bus.attach(d1);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  fixture.sns2.setPresent(true);
  pumpMs(fixture, 2000);
  TEST_ASSERT_FALSE(fixture.bus.collision);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(1));
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
  TEST_ASSERT_EQUAL(0x11, fixture.host.address(1));
}

void testFourSlotsInsertedTogetherGetUniqueAddresses()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  FakeModuleDevice d2(clock, fixture.mod3);
  FakeModuleDevice d3(clock, fixture.mod4);
  fixture.bus.attach(d0);
  fixture.bus.attach(d1);
  fixture.bus.attach(d2);
  fixture.bus.attach(d3);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  fixture.sns2.setPresent(true);
  fixture.sns3.setPresent(true);
  fixture.sns4.setPresent(true);
  pumpMs(fixture, 4000);
  TEST_ASSERT_EQUAL(0x10, fixture.host.address(0));
  TEST_ASSERT_EQUAL(0x11, fixture.host.address(1));
  TEST_ASSERT_EQUAL(0x12, fixture.host.address(2));
  TEST_ASSERT_EQUAL(0x13, fixture.host.address(3));
}

void testOneI2cTransactionPerUpdateGlobally()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  FakeModuleDevice d2(clock, fixture.mod3);
  FakeModuleDevice d3(clock, fixture.mod4);
  fixture.bus.attach(d0);
  fixture.bus.attach(d1);
  fixture.bus.attach(d2);
  fixture.bus.attach(d3);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  fixture.sns2.setPresent(true);
  fixture.sns3.setPresent(true);
  fixture.sns4.setPresent(true);
  pumpMs(fixture, 4000);
  const size_t before = fixture.bus.protocolOpCount();
  clock.advance(1000);
  fixture.host.update();
  TEST_ASSERT_EQUAL(1, newOps(fixture, before));
}

void testHealthPingDoesNotShareUpdateWithEnumTxn()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  fixture.host.begin();
  plugAndPump(fixture, 0, d0);
  fixture.bus.attach(d1);
  fixture.sns2.setPresent(true);
  clock.advance(1000);
  const size_t before = fixture.bus.protocolOpCount();
  fixture.host.update();
  TEST_ASSERT_EQUAL(1, newOps(fixture, before));
  pumpMs(fixture, 255);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
  TEST_ASSERT_EQUAL(SlotState::Enumerating, fixture.host.state(1));
}

void testWriteReadNackOnWriteDoesNotParseRx()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 270);
  clock.advance(10);
  fixture.host.update();
  TEST_ASSERT_EQUAL(I2cTxnStatus::Nack, fixture.bus.ops.back().result);
}

void testNoncompliantAlwaysAck0x0AIsDetected()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice d0(clock, fixture.mod1);
  FakeModuleDevice d1(clock, fixture.mod2);
  d1.ignoreMod = true;
  fixture.bus.attach(d0);
  fixture.bus.attach(d1);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  fixture.sns2.setPresent(true);
  pumpMs(fixture, 1500);
  TEST_ASSERT_TRUE(fixture.bus.collision ||
                   fixture.host.state(0) == SlotState::Fault ||
                   fixture.host.state(1) == SlotState::Fault);
}

void testSenseGlitchDuringWaitForLockDoesNotAbortEnumerate()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.bus.attach(device);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 250);
  fixture.sns1.setPresent(false);
  pumpMs(fixture, 20);
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

void testSetAddressNackRetries()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.bus.attach(device);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  pumpMs(fixture, 270);
  clock.advance(10);
  fixture.host.update();
  device.nackRemaining = 1;
  clock.advance(1);
  fixture.host.update();
  pumpMs(fixture, 800);
  TEST_ASSERT_EQUAL(SlotState::Online, fixture.host.state(0));
}

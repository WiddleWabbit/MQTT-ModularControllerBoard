#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "IspProgrammer.h"
#include "ProgrammingSession.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeBytePort.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeModuleDevice.h"
#include "fakes/FakeSpiMaster.h"

namespace
{
const uint8_t kInsync = 0x14;
const uint8_t kOk = 0x10;
const uint8_t kFailed = 0x11;
const uint8_t kUnknown = 0x12;
const uint8_t kNosync = 0x15;
const uint8_t kEop = 0x20;

/**
 * Pin that records the moment reset is driven low, after its mode is output.
 */
class TracePin : public IDigitalPin
{
public:
  /**
   * Creates a pin that appends "reset-low" to events.
   *
   * @param events Shared event log. May be null.
   */
  explicit TracePin(std::vector<std::string>* events) : _events(events)
  {
  }

  /**
   * Stores the electrical mode.
   *
   * @param mode Requested mode.
   * @return Nothing.
   */
  void setMode(PinMode mode) override
  {
    this->mode = mode;
  }

  /**
   * Reads the last driven level.
   *
   * @return True when HIGH.
   */
  bool read() const override
  {
    return level;
  }

  /**
   * Drives the pin. Logs reset-low when an output is driven low.
   *
   * @param value True for HIGH.
   * @return Nothing.
   */
  void write(bool value) override
  {
    if (mode == PinMode::DigitalOutput ||
        mode == PinMode::DigitalOutputOpenDrain)
    {
      level = value;
    }
    if (mode == PinMode::DigitalOutput && !value && _events != nullptr)
    {
      _events->push_back("reset-low");
    }
  }

  PinMode mode = PinMode::DigitalInput;
  bool level = true;

private:
  std::vector<std::string>* _events;
};

/**
 * Fails the test unless actual matches expected.
 *
 * @param actual Bytes produced by the test.
 * @param expected Expected sequence.
 * @param length Number of expected bytes.
 * @return Nothing.
 */
void assertBytes(const std::vector<uint8_t>& actual, const uint8_t* expected,
                 size_t length)
{
  TEST_ASSERT_EQUAL(length, actual.size());
  if (length == 0)
  {
    return;
  }
  TEST_ASSERT_EQUAL_MEMORY(expected, actual.data(), length);
}

/**
 * Programmer, byte port, SPI master, reset pin, and clock for one test.
 */
struct IspFixture
{
  FakeBytePort port;
  FakeSpiMaster spi;
  FakeDigitalPin reset;
  FakeClock clock;
  IspProgrammer programmer;

  IspFixture() : programmer(port, spi, reset, clock)
  {
  }
};
}

void testIspSyncAndSignOn()
{
  IspFixture isp;
  const uint8_t sync[] = {0x30, kEop};
  isp.port.feed(sync, sizeof(sync));
  isp.programmer.update();
  const uint8_t syncReply[] = {kInsync, kOk};
  assertBytes(isp.port.output, syncReply, sizeof(syncReply));
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());

  isp.port.output.clear();
  const uint8_t signOn[] = {0x31, kEop};
  isp.port.feed(signOn, sizeof(signOn));
  isp.programmer.update();
  const uint8_t signReply[] = {
    kInsync, 'A', 'V', 'R', ' ', 'I', 'S', 'P', kOk};
  assertBytes(isp.port.output, signReply, sizeof(signReply));
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());
}

void testIspResyncAfterNoise()
{
  IspFixture isp;
  const uint8_t noise[] = {0x99, 0x01};
  isp.port.feed(noise, sizeof(noise));
  isp.programmer.update();
  const uint8_t nosync[] = {kNosync};
  assertBytes(isp.port.output, nosync, sizeof(nosync));
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());

  isp.port.output.clear();
  const uint8_t sync[] = {0x30, kEop};
  isp.port.feed(sync, sizeof(sync));
  isp.programmer.update();
  const uint8_t ok[] = {kInsync, kOk};
  assertBytes(isp.port.output, ok, sizeof(ok));
}

void testIspUnknownCommandDoesNotUseSpi()
{
  IspFixture isp;
  const uint8_t command[] = {0x99, kEop};
  isp.port.feed(command, sizeof(command));
  isp.programmer.update();
  const uint8_t reply[] = {kUnknown};
  assertBytes(isp.port.output, reply, sizeof(reply));
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());
  TEST_ASSERT_EQUAL(0, isp.spi.beginCount);
}

void testIspPartialCommandWaitsForEnd()
{
  IspFixture isp;
  const uint8_t partial[] = {0x30};
  isp.port.feed(partial, sizeof(partial));
  isp.programmer.update();
  TEST_ASSERT_EQUAL(0, isp.port.output.size());
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());

  const uint8_t eop[] = {kEop};
  isp.port.feed(eop, sizeof(eop));
  isp.programmer.update();
  const uint8_t ok[] = {kInsync, kOk};
  assertBytes(isp.port.output, ok, sizeof(ok));
}

void testIspEnterProgrammingEnableAfterResetSettle()
{
  std::vector<std::string> events;
  FakeBytePort port;
  FakeSpiMaster spi;
  spi.events = &events;
  spi.script = {0x00, 0x00, 0x53, 0x00};
  TracePin reset(&events);
  FakeClock clock;
  IspProgrammer programmer(port, spi, reset, clock);

  const uint8_t enter[] = {0x50, kEop};
  port.feed(enter, sizeof(enter));
  programmer.update();
  TEST_ASSERT_EQUAL(2, events.size());
  TEST_ASSERT_EQUAL_STRING("spi-begin", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("reset-low", events[1].c_str());
  TEST_ASSERT_EQUAL(0, spi.sent.size());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, reset.mode);
  TEST_ASSERT_FALSE(reset.level);
  TEST_ASSERT_EQUAL(1, spi.beginCount);
  TEST_ASSERT_EQUAL(125000, spi.clockHz);

  clock.advance(19);
  programmer.update();
  TEST_ASSERT_EQUAL(0, spi.sent.size());

  clock.advance(1);
  programmer.update();
  const uint8_t enable[] = {0xAC, 0x53, 0x00, 0x00};
  assertBytes(spi.sent, enable, sizeof(enable));
  const uint8_t ok[] = {kInsync, kOk};
  assertBytes(port.output, ok, sizeof(ok));
  TEST_ASSERT_FALSE(reset.level);

  port.output.clear();
  port.feed(enter, sizeof(enter));
  programmer.update();
  assertBytes(port.output, ok, sizeof(ok));
  TEST_ASSERT_EQUAL(1, spi.beginCount);
  TEST_ASSERT_EQUAL(sizeof(enable), spi.sent.size());
  TEST_ASSERT_FALSE(reset.level);
  TEST_ASSERT_EQUAL(2, events.size());
}

void testIspEnterFailsAfterThreeEnables()
{
  IspFixture isp;
  const uint8_t enter[] = {0x50, kEop};
  isp.port.feed(enter, sizeof(enter));
  isp.programmer.update();
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());
  TEST_ASSERT_FALSE(isp.reset.level);

  isp.clock.advance(20);
  isp.programmer.update();
  isp.clock.advance(20);
  isp.programmer.update();
  isp.clock.advance(20);
  isp.programmer.update();

  TEST_ASSERT_EQUAL(12, isp.spi.sent.size());
  const uint8_t failed[] = {kInsync, kFailed};
  assertBytes(isp.port.output, failed, sizeof(failed));
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, isp.reset.mode);
  TEST_ASSERT_TRUE(isp.reset.level);
  TEST_ASSERT_EQUAL(0, isp.spi.endCount);
}

void testIspLeaveReleasesReset()
{
  IspFixture isp;
  isp.spi.script = {0x00, 0x00, 0x53, 0x00};
  const uint8_t enter[] = {0x50, kEop};
  isp.port.feed(enter, sizeof(enter));
  isp.programmer.update();
  isp.clock.advance(20);
  isp.programmer.update();
  TEST_ASSERT_FALSE(isp.reset.level);
  TEST_ASSERT_TRUE(isp.spi.begun);

  isp.port.output.clear();
  const uint8_t leave[] = {0x51, kEop};
  isp.port.feed(leave, sizeof(leave));
  isp.programmer.update();
  const uint8_t ok[] = {kInsync, kOk};
  assertBytes(isp.port.output, ok, sizeof(ok));
  TEST_ASSERT_EQUAL(1, isp.spi.endCount);
  TEST_ASSERT_FALSE(isp.spi.begun);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, isp.reset.mode);
  TEST_ASSERT_TRUE(isp.reset.level);
}

void testIspReadSignature()
{
  IspFixture isp;
  isp.spi.script = {
    0x00, 0x00, 0x00, 0x1E,
    0x00, 0x00, 0x00, 0x95,
    0x00, 0x00, 0x00, 0x16};
  const uint8_t command[] = {0x75, kEop};
  isp.port.feed(command, sizeof(command));
  isp.programmer.update();
  const uint8_t reply[] = {kInsync, 0x1E, 0x95, 0x16, kOk};
  assertBytes(isp.port.output, reply, sizeof(reply));
  const uint8_t spi[] = {
    0x30, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x01, 0x00,
    0x30, 0x00, 0x02, 0x00};
  assertBytes(isp.spi.sent, spi, sizeof(spi));
}

void testIspUniversalReturnsFourthByte()
{
  IspFixture isp;
  isp.spi.script = {0x11, 0x22, 0x33, 0xAB};
  const uint8_t command[] = {0x56, 0xA0, 0x01, 0x02, 0x03, kEop};
  isp.port.feed(command, sizeof(command));
  isp.programmer.update();
  const uint8_t sent[] = {0xA0, 0x01, 0x02, 0x03};
  assertBytes(isp.spi.sent, sent, sizeof(sent));
  const uint8_t reply[] = {kInsync, 0xAB, kOk};
  assertBytes(isp.port.output, reply, sizeof(reply));
}

void testIspLoadAddressIsLittleEndianWord()
{
  IspFixture isp;
  uint8_t setDevice[22] = {};
  setDevice[0] = 0x42;
  setDevice[13] = 0x00;
  setDevice[14] = 0x80;
  setDevice[21] = kEop;
  isp.port.feed(setDevice, sizeof(setDevice));
  isp.programmer.update();
  const uint8_t staged[] = {kInsync, kOk};
  assertBytes(isp.port.output, staged, sizeof(staged));

  isp.port.output.clear();
  const uint8_t address[] = {0x55, 0x34, 0x12, kEop};
  isp.port.feed(address, sizeof(address));
  isp.programmer.update();
  assertBytes(isp.port.output, staged, sizeof(staged));

  const uint8_t page[] = {0x64, 0x00, 0x02, 'F', 0x11, 0x22, kEop};
  isp.port.feed(page, sizeof(page));
  isp.programmer.update();
  const uint8_t spi[] = {
    0x40, 0x12, 0x34, 0x11,
    0x48, 0x12, 0x34, 0x22,
    0x4C, 0x12, 0x00, 0x00};
  assertBytes(isp.spi.sent, spi, sizeof(spi));
}

void testIspProgramPageWaitsBeforeNextTransfer()
{
  IspFixture isp;
  const uint8_t address[] = {0x55, 0x34, 0x12, kEop};
  isp.port.feed(address, sizeof(address));
  isp.programmer.update();
  const uint8_t page[] = {0x64, 0x00, 0x02, 'F', 0x11, 0x22, kEop};
  isp.port.feed(page, sizeof(page));
  isp.programmer.update();
  const uint8_t spi[] = {
    0x40, 0x12, 0x34, 0x11,
    0x48, 0x12, 0x34, 0x22,
    0x4C, 0x12, 0x34, 0x00};
  assertBytes(isp.spi.sent, spi, sizeof(spi));
  TEST_ASSERT_EQUAL(2, isp.port.output.size());

  const uint8_t universal[] = {0x56, 0x11, 0x22, 0x33, 0x44, kEop};
  isp.port.feed(universal, sizeof(universal));
  isp.clock.advance(4);
  isp.programmer.update();
  TEST_ASSERT_EQUAL(12, isp.spi.sent.size());
  TEST_ASSERT_EQUAL(2, isp.port.output.size());

  isp.spi.script = {0x01, 0x02, 0x03, 0xAB};
  isp.clock.advance(1);
  isp.programmer.update();
  TEST_ASSERT_EQUAL(16, isp.spi.sent.size());
  TEST_ASSERT_EQUAL_UINT8(0x11, isp.spi.sent[12]);
  TEST_ASSERT_EQUAL_UINT8(0x22, isp.spi.sent[13]);
  TEST_ASSERT_EQUAL_UINT8(0x33, isp.spi.sent[14]);
  TEST_ASSERT_EQUAL_UINT8(0x44, isp.spi.sent[15]);
  const uint8_t done[] = {kInsync, kOk, kInsync, 0xAB, kOk};
  TEST_ASSERT_EQUAL(2 + sizeof(done), isp.port.output.size());
  TEST_ASSERT_EQUAL_MEMORY(done, isp.port.output.data() + 2, sizeof(done));
}

void testIspReadPageAssemblesFlashBytes()
{
  IspFixture isp;
  const uint8_t address[] = {0x55, 0x10, 0x00, kEop};
  isp.port.feed(address, sizeof(address));
  isp.programmer.update();
  isp.spi.script = {
    0x00, 0x00, 0x00, 0xAB,
    0x00, 0x00, 0x00, 0xCD};
  isp.port.output.clear();
  const uint8_t page[] = {0x74, 0x00, 0x02, 'F', kEop};
  isp.port.feed(page, sizeof(page));
  isp.programmer.update();
  const uint8_t reply[] = {kInsync, 0xAB, 0xCD, kOk};
  assertBytes(isp.port.output, reply, sizeof(reply));
  const uint8_t spi[] = {
    0x20, 0x00, 0x10, 0x00,
    0x28, 0x00, 0x10, 0x00};
  assertBytes(isp.spi.sent, spi, sizeof(spi));
}

void testIspProgramEepromUsesEepromOpcode()
{
  IspFixture isp;
  const uint8_t address[] = {0x55, 0x10, 0x00, kEop};
  isp.port.feed(address, sizeof(address));
  isp.programmer.update();
  const uint8_t page[] = {0x64, 0x00, 0x02, 'E', 0xAA, 0xBB, kEop};
  isp.port.feed(page, sizeof(page));
  isp.programmer.update();
  const uint8_t first[] = {0xC0, 0x00, 0x20, 0xAA};
  assertBytes(isp.spi.sent, first, sizeof(first));
  TEST_ASSERT_EQUAL(2, isp.port.output.size());

  isp.clock.advance(9);
  isp.programmer.update();
  TEST_ASSERT_EQUAL(4, isp.spi.sent.size());

  isp.clock.advance(1);
  isp.programmer.update();
  const uint8_t both[] = {
    0xC0, 0x00, 0x20, 0xAA,
    0xC0, 0x00, 0x21, 0xBB};
  assertBytes(isp.spi.sent, both, sizeof(both));
  TEST_ASSERT_EQUAL(2, isp.port.output.size());

  isp.clock.advance(10);
  isp.programmer.update();
  const uint8_t ok[] = {kInsync, kOk};
  TEST_ASSERT_EQUAL(4, isp.port.output.size());
  TEST_ASSERT_EQUAL_MEMORY(ok, isp.port.output.data() + 2, sizeof(ok));

  isp.spi.script = {0x00, 0x00, 0x00, 0x77};
  isp.spi.scriptIndex = 0;
  isp.port.output.clear();
  const uint8_t read[] = {0x74, 0x00, 0x01, 'E', kEop};
  isp.port.feed(read, sizeof(read));
  isp.programmer.update();
  const uint8_t readSpi[] = {0xA0, 0x00, 0x20, 0xFF};
  TEST_ASSERT_EQUAL(12, isp.spi.sent.size());
  TEST_ASSERT_EQUAL_MEMORY(readSpi, isp.spi.sent.data() + 8, sizeof(readSpi));
  const uint8_t readReply[] = {kInsync, 0x77, kOk};
  assertBytes(isp.port.output, readReply, sizeof(readReply));
}

void testIspChipEraseWaits()
{
  IspFixture isp;
  const uint8_t erase[] = {0x52, kEop};
  isp.port.feed(erase, sizeof(erase));
  isp.programmer.update();
  const uint8_t command[] = {0xAC, 0x80, 0x00, 0x00};
  assertBytes(isp.spi.sent, command, sizeof(command));
  TEST_ASSERT_EQUAL(0, isp.port.output.size());

  isp.clock.advance(9);
  isp.programmer.update();
  TEST_ASSERT_EQUAL(0, isp.port.output.size());
  isp.clock.advance(1);
  isp.programmer.update();
  const uint8_t ok[] = {kInsync, kOk};
  assertBytes(isp.port.output, ok, sizeof(ok));

  isp.spi.sent.clear();
  isp.port.output.clear();
  isp.spi.script = {0x01, 0x02, 0x03, 0x44};
  isp.spi.scriptIndex = 0;
  const uint8_t universal[] = {0x56, 0xAC, 0x80, 0x00, 0x00, kEop};
  isp.port.feed(universal, sizeof(universal));
  isp.programmer.update();
  assertBytes(isp.spi.sent, command, sizeof(command));
  TEST_ASSERT_EQUAL(0, isp.port.output.size());
  isp.clock.advance(10);
  isp.programmer.update();
  const uint8_t reply[] = {kInsync, 0x44, kOk};
  assertBytes(isp.port.output, reply, sizeof(reply));
}

void testIspSetParameterDoesNotChangeSpiClock()
{
  IspFixture isp;
  const uint8_t setDuration[] = {0x40, 0x89, 0x02, kEop};
  isp.port.feed(setDuration, sizeof(setDuration));
  isp.programmer.update();

  const uint8_t hw[] = {0x41, 0x80, kEop};
  const uint8_t major[] = {0x41, 0x81, kEop};
  const uint8_t minor[] = {0x41, 0x82, kEop};
  const uint8_t top[] = {0x41, 0x93, kEop};
  const uint8_t stored[] = {0x41, 0x89, kEop};
  const uint8_t missing[] = {0x41, 0x84, kEop};
  const uint8_t* gets[] = {hw, major, minor, top, stored, missing};
  const uint8_t values[] = {2, 1, 18, 'S', 0x02, 0x00};
  for (size_t i = 0; i < 6; ++i)
  {
    isp.port.output.clear();
    isp.port.feed(gets[i], 3);
    isp.programmer.update();
    const uint8_t reply[] = {kInsync, values[i], kOk};
    assertBytes(isp.port.output, reply, sizeof(reply));
  }

  isp.spi.script = {0x00, 0x00, 0x53, 0x00};
  const uint8_t enter[] = {0x50, kEop};
  isp.port.feed(enter, sizeof(enter));
  isp.programmer.update();
  TEST_ASSERT_EQUAL(1, isp.spi.beginCount);
  TEST_ASSERT_EQUAL(125000, isp.spi.clockHz);
  TEST_ASSERT_EQUAL(0, isp.spi.sent.size());
  isp.clock.advance(20);
  isp.programmer.update();
  TEST_ASSERT_EQUAL(1, isp.spi.beginCount);
  TEST_ASSERT_EQUAL(125000, isp.spi.clockHz);
  TEST_ASSERT_EQUAL(4, isp.spi.sent.size());
}

void testProgrammingSessionQuiescesHostUntilIdleTimeout()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  FakeModuleDevice device(clock, fixture.mod1);
  fixture.bus.attach(device);
  fixture.host.begin();
  fixture.sns1.setPresent(true);
  bool sawMod = false;
  for (uint32_t i = 0; i < 400; ++i)
  {
    clock.advance(1);
    fixture.host.update();
    if (fixture.mod1.mode == PinMode::DigitalOutputOpenDrain)
    {
      sawMod = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(sawMod);
  const size_t ops = fixture.bus.protocolOpCount();

  FakeBytePort port;
  FakeSpiMaster spi;
  IspProgrammer programmer(port, spi, fixture.cs1, clock);
  ProgrammingSession session(fixture.host, port, clock, 60000, 1000);
  programmer.start(fixture.cs1, 6);
  session.begin(programmer);
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);
  TEST_ASSERT_TRUE(fixture.cs1.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, fixture.mod1.mode);

  const uint32_t started = clock.millis();
  clock.set(started + 59999);
  fixture.host.update();
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(ops, fixture.bus.protocolOpCount());

  const uint8_t sync[] = {0x30, kEop};
  port.feed(sync, sizeof(sync));
  session.update();
  const uint8_t syncReply[] = {kInsync, kOk};
  assertBytes(port.output, syncReply, sizeof(syncReply));
  const uint32_t refreshed = clock.millis();
  clock.set(refreshed + 59999);
  fixture.host.update();
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(ops, fixture.bus.protocolOpCount());

  clock.set(refreshed + 60000);
  session.update();
  TEST_ASSERT_FALSE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs1.mode);
  TEST_ASSERT_EQUAL(ops, fixture.bus.protocolOpCount());

  for (uint32_t i = 0; i < 100; ++i)
  {
    clock.advance(1);
    fixture.host.update();
  }
  TEST_ASSERT_TRUE(fixture.bus.protocolOpCount() > ops);
}

void testProgrammingSessionStaysUpWhilePortUnseen()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  FakeBytePort port;
  port.plugged = false;
  FakeSpiMaster spi;
  IspProgrammer programmer(port, spi, fixture.cs1, clock);
  ProgrammingSession session(fixture.host, port, clock, 60000, 1000);
  programmer.start(fixture.cs1, 6);
  session.begin(programmer);

  clock.set(59999);
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);

  clock.set(60000);
  session.update();
  TEST_ASSERT_FALSE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs1.mode);
}

void testProgrammingSessionSurvivesBriefUnplug()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  FakeBytePort port;
  FakeSpiMaster spi;
  IspProgrammer programmer(port, spi, fixture.cs1, clock);
  ProgrammingSession session(fixture.host, port, clock, 60000, 1000);
  programmer.start(fixture.cs1, 6);
  session.begin(programmer);
  session.update();
  TEST_ASSERT_TRUE(session.active());

  port.plugged = false;
  session.update();
  clock.advance(999);
  const uint8_t sync[] = {0x30, kEop};
  port.feed(sync, sizeof(sync));
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);
  const uint8_t syncReply[] = {kInsync, kOk};
  assertBytes(port.output, syncReply, sizeof(syncReply));

  port.plugged = true;
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);
}

void testProgrammingSessionEndsWhenUnplugged()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  FakeBytePort port;
  FakeSpiMaster spi;
  IspProgrammer programmer(port, spi, fixture.cs1, clock);
  ProgrammingSession session(fixture.host, port, clock, 60000, 1000);
  programmer.start(fixture.cs1, 6);
  session.begin(programmer);
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);

  port.plugged = false;
  session.update();
  clock.advance(999);
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);

  clock.advance(1);
  session.update();
  TEST_ASSERT_FALSE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, fixture.cs1.mode);

  programmer.start(fixture.cs1, 6);
  session.begin(programmer);
  clock.advance(1000);
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutput, fixture.cs1.mode);
}


// ========== Runner ==========

/**
 * Registers the STK500 and programming-session cases.
 *
 * @return Nothing.
 */
void runProgrammingTests()
{
  // UNITY_BEGIN records test_main.cpp, so name this file for the report.
  UnitySetTestFile(__FILE__);
  RUN_TEST(testIspSyncAndSignOn);
  RUN_TEST(testIspResyncAfterNoise);
  RUN_TEST(testIspUnknownCommandDoesNotUseSpi);
  RUN_TEST(testIspPartialCommandWaitsForEnd);
  RUN_TEST(testIspEnterProgrammingEnableAfterResetSettle);
  RUN_TEST(testIspEnterFailsAfterThreeEnables);
  RUN_TEST(testIspLeaveReleasesReset);
  RUN_TEST(testIspReadSignature);
  RUN_TEST(testIspUniversalReturnsFourthByte);
  RUN_TEST(testIspLoadAddressIsLittleEndianWord);
  RUN_TEST(testIspProgramPageWaitsBeforeNextTransfer);
  RUN_TEST(testIspReadPageAssemblesFlashBytes);
  RUN_TEST(testIspProgramEepromUsesEepromOpcode);
  RUN_TEST(testIspChipEraseWaits);
  RUN_TEST(testIspSetParameterDoesNotChangeSpiClock);
  RUN_TEST(testProgrammingSessionQuiescesHostUntilIdleTimeout);
  RUN_TEST(testProgrammingSessionStaysUpWhilePortUnseen);
  RUN_TEST(testProgrammingSessionSurvivesBriefUnplug);
  RUN_TEST(testProgrammingSessionEndsWhenUnplugged);
}

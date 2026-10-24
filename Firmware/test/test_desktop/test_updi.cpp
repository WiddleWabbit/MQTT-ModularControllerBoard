#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "ProgrammingSession.h"
#include "UpdiProgrammer.h"
#include "fakes/EmptyModuleHostFixture.h"
#include "fakes/FakeBytePort.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeDigitalPin.h"
#include "fakes/FakeHalfDuplexUart.h"
#include "fakes/FakeSpiMaster.h"

namespace
{
const uint8_t kNvmKey[] = {0x55, 0xE0, 0x20, 0x67, 0x6F, 0x72, 0x50, 0x4D, 0x56, 0x4E};
const uint8_t kEraseKey[] = {0x55, 0xE0, 0x65, 0x73, 0x61, 0x72, 0x45, 0x4D, 0x56, 0x4E};
const uint8_t kSignOn[29] = {
  0x86, 0x01,
  0x01, 0x00, 0x06, 0x01,
  0x01, 0x00, 0x06, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  'J', 'T', 'A', 'G', 'I', 'C', 'E', ' ', 'm', 'k', 'I', 'I', 0};

/**
 * AVR067 CRC-16 over a buffer. Init 0xFFFF, reflected polynomial 0x8408.
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
        crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408);
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
std::vector<uint8_t> frame(uint16_t sequence, const uint8_t* body, size_t length)
{
  std::vector<uint8_t> bytes;
  bytes.push_back(0x1B);
  bytes.push_back(static_cast<uint8_t>(sequence & 0xFF));
  bytes.push_back(static_cast<uint8_t>(sequence >> 8));
  bytes.push_back(static_cast<uint8_t>(length & 0xFF));
  bytes.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.push_back(0x0E);
  for (size_t index = 0; index < length; ++index)
  {
    bytes.push_back(body[index]);
  }
  const uint16_t crc = crc16Jtag(bytes.data(), bytes.size());
  bytes.push_back(static_cast<uint8_t>(crc & 0xFF));
  bytes.push_back(static_cast<uint8_t>(crc >> 8));
  return bytes;
}

/**
 * Programmer, port, UART, and pin for one UPDI case.
 */
struct UpdiFixture
{
  FakeBytePort port;
  FakeHalfDuplexUart uart;
  FakeDigitalPin line;
  FakeClock clock;
  UpdiProgrammer programmer;

  UpdiFixture()
    : programmer(port, uart, clock)
  {
    programmer.start(line, 6);
  }

  /**
   * Feeds a command body and lets the programmer read it.
   *
   * @param sequence Sequence number.
   * @param body Command body.
   * @param length Body length.
   * @return Nothing.
   */
  void send(uint16_t sequence, const uint8_t* body, size_t length)
  {
    const std::vector<uint8_t> bytes = frame(sequence, body, length);
    port.feed(bytes.data(), bytes.size());
    programmer.update();
  }
};

/**
 * Reports whether needle occurs in order inside hay.
 *
 * @param hay Bytes to search.
 * @param needle Sequence to find.
 * @param length Needle length.
 * @return True when the sequence is present.
 */
bool contains(const std::vector<uint8_t>& hay, const uint8_t* needle, size_t length)
{
  if (length == 0 || hay.size() < length)
  {
    return false;
  }
  for (size_t start = 0; start + length <= hay.size(); ++start)
  {
    bool match = true;
    for (size_t index = 0; index < length; ++index)
    {
      if (hay[start + index] != needle[index])
      {
        match = false;
        break;
      }
    }
    if (match)
    {
      return true;
    }
  }
  return false;
}

/**
 * Counts non-overlapping occurrences of needle inside hay.
 *
 * @param hay Bytes to search.
 * @param needle Sequence to count.
 * @param length Needle length.
 * @return How many times the sequence occurs.
 */
int countOf(const std::vector<uint8_t>& hay, const uint8_t* needle, size_t length)
{
  int count = 0;
  if (length == 0 || hay.size() < length)
  {
    return 0;
  }
  for (size_t start = 0; start + length <= hay.size();)
  {
    bool match = true;
    for (size_t index = 0; index < length; ++index)
    {
      if (hay[start + index] != needle[index])
      {
        match = false;
        break;
      }
    }
    if (match)
    {
      count++;
      start += length;
    }
    else
    {
      start++;
    }
  }
  return count;
}

/**
 * Parses one answer frame from the USB port.
 *
 * @param bytes Port output.
 * @param sequence Receives the sequence number.
 * @param body Receives the body.
 * @return False when the frame is truncated or the CRC mismatches.
 */
bool parse(const std::vector<uint8_t>& bytes, uint16_t& sequence,
           std::vector<uint8_t>& body)
{
  if (bytes.size() < 10 || bytes[0] != 0x1B || bytes[7] != 0x0E)
  {
    return false;
  }
  const uint32_t size = static_cast<uint32_t>(bytes[3]) |
                        (static_cast<uint32_t>(bytes[4]) << 8) |
                        (static_cast<uint32_t>(bytes[5]) << 16) |
                        (static_cast<uint32_t>(bytes[6]) << 24);
  if (bytes.size() != 8u + size + 2u)
  {
    return false;
  }
  const uint16_t expect = crc16Jtag(bytes.data(), 8u + size);
  const uint16_t got = static_cast<uint16_t>(
    bytes[8 + size] | (bytes[9 + size] << 8));
  if (expect != got)
  {
    return false;
  }
  sequence = static_cast<uint16_t>(bytes[1] | (bytes[2] << 8));
  body.assign(bytes.begin() + 8, bytes.begin() + 8 + static_cast<int>(size));
  return true;
}

/**
 * Connects with a 64-byte flash page and runs the 25/3/25 break.
 *
 * @param rig Fixture whose pin is already bound.
 * @return Nothing.
 */
void connect(UpdiFixture& rig)
{
  uint8_t body[247] = {};
  body[0] = 0x0C;
  body[244] = 64;
  body[246] = 32;
  const std::vector<uint8_t> bytes = frame(7, body, sizeof(body));
  rig.port.feed(bytes.data(), bytes.size());
  rig.programmer.update();
  rig.clock.advance(25);
  rig.programmer.update();
  rig.clock.advance(3);
  rig.programmer.update();
  rig.clock.advance(25);
  rig.programmer.update();
}

/**
 * Queues the replies for enter from normal mode, including the NVM key.
 *
 * @param uart UART whose reply queue receives the script.
 * @return Nothing.
 */
void scriptEnterFromRun(FakeHalfDuplexUart& uart)
{
  const uint8_t replies[] = {0x82, 0x08, 0x00, 0x08, 0x40, 0x40};
  uart.reply(replies, sizeof(replies));
}

/**
 * Queues enter replies whose first reset polls are still clear.
 * Four 0x00 bytes, then BOOTDONE in bit 1.
 *
 * @param uart UART whose reply queue receives the script.
 * @return Nothing.
 */
void scriptEnterAfterSlowBoot(FakeHalfDuplexUart& uart)
{
  const uint8_t replies[] = {
    0x82, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x40, 0x40};
  uart.reply(replies, sizeof(replies));
}

/**
 * Counts recorded writes of one length.
 *
 * @param sizes Recorded write lengths.
 * @param length Length to count.
 * @return How many writes had that length.
 */
int writesOf(const std::vector<size_t>& sizes, size_t length)
{
  int count = 0;
  for (size_t entry : sizes)
  {
    if (entry == length)
    {
      count++;
    }
  }
  return count;
}

/**
 * Clock that can step forward on every millis() call.
 */
class StepClock : public IClock
{
public:
  /**
   * Returns the current time. When autoStep is set, each call adds
   * stepMs first.
   *
   * @return Current time in milliseconds.
   */
  uint32_t millis() const override
  {
    if (autoStep)
    {
      _now += stepMs;
    }
    return _now;
  }

  /**
   * Moves the clock forward without a per-call step.
   *
   * @param milliseconds Amount of time to advance.
   * @return Nothing.
   */
  void advance(uint32_t milliseconds)
  {
    _now += milliseconds;
  }

  bool autoStep = false;
  uint32_t stepMs = 25;

private:
  mutable uint32_t _now = 0;
};

/**
 * Connects a StepClock programmer and runs the 25/3/25 break.
 * autoStep stays off so the break uses advance().
 *
 * @param programmer Programmer already started on its pin.
 * @param port USB port that receives the descriptor.
 * @param clock Clock advanced by the break phases.
 * @return Nothing.
 */
void connectStep(UpdiProgrammer& programmer, FakeBytePort& port, StepClock& clock)
{
  uint8_t descriptor[247] = {};
  descriptor[0] = 0x0C;
  descriptor[244] = 64;
  descriptor[246] = 32;
  const std::vector<uint8_t> setup = frame(7, descriptor, sizeof(descriptor));
  port.feed(setup.data(), setup.size());
  programmer.update();
  clock.advance(25);
  programmer.update();
  clock.advance(3);
  programmer.update();
  clock.advance(25);
  programmer.update();
}

/**
 * Queues the replies for one aligned 64-byte flash page write.
 *
 * @param uart UART whose reply queue receives the script.
 * @return Nothing.
 */
void scriptPageWrite(FakeHalfDuplexUart& uart)
{
  const uint8_t head[] = {0x08, 0x40, 0x00, 0x00, 0x00, 0x40};
  uart.reply(head, sizeof(head));
  for (int index = 0; index < 64; ++index)
  {
    uart.reply(0x40);
  }
  const uint8_t commit[] = {0x11, 0x22, 0x40, 0x40, 0x40};
  uart.reply(commit, sizeof(commit));
}
}


// ========== Cases ==========

void testCrc16JtagKnownVector()
{
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x6F91, crc16Jtag(text, sizeof(text)));
}

void testUpdiSignOnReturnsJtagiceBody()
{
  UpdiFixture rig;
  const uint8_t body[] = {0x01};
  rig.send(0x1234, body, sizeof(body));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX16(0x1234, sequence);
  TEST_ASSERT_EQUAL(29, answer.size());
  TEST_ASSERT_EQUAL_MEMORY(kSignOn, answer.data(), 29);
  TEST_ASSERT_EQUAL(0, rig.uart.installCount);
}

void testUpdiDescriptorDoubleBreakThenControlA()
{
  UpdiFixture rig;
  uint8_t body[247] = {};
  body[0] = 0x0C;
  body[244] = 64;
  const std::vector<uint8_t> bytes = frame(3, body, sizeof(body));
  rig.port.feed(bytes.data(), bytes.size());
  rig.programmer.update();
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.installed);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(1, rig.uart.installCount);
  TEST_ASSERT_EQUAL(0, rig.uart.claimCount);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  TEST_ASSERT_EQUAL(0, rig.port.output.size());

  rig.clock.advance(24);
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());

  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, rig.line.mode);
  TEST_ASSERT_TRUE(rig.line.read());
  TEST_ASSERT_FALSE(rig.uart.attached);
  rig.clock.advance(2);
  rig.programmer.update();
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, rig.line.mode);
  TEST_ASSERT_TRUE(rig.line.read());
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  rig.clock.advance(24);
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());

  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_TRUE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.attached);
  TEST_ASSERT_EQUAL(1, rig.uart.installCount);
  TEST_ASSERT_EQUAL(1, rig.uart.claimCount);
  TEST_ASSERT_EQUAL(6, rig.uart.gpio);
  TEST_ASSERT_EQUAL(115200, rig.uart.baud);
  const uint8_t stcs[] = {0x55, 0xC2, 0x00};
  TEST_ASSERT_TRUE(contains(rig.uart.tx, stcs, sizeof(stcs)));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL(1, answer.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
}

void testUpdiGroupsInstructionIntoOneWrite()
{
  UpdiFixture rig;
  connect(rig);
  TEST_ASSERT_EQUAL(1, rig.uart.writeSizes.size());
  TEST_ASSERT_EQUAL(3, rig.uart.writeSizes[0]);
  const uint8_t stcs[] = {0x55, 0xC2, 0x00};
  TEST_ASSERT_EQUAL(3, rig.uart.tx.size());
  TEST_ASSERT_EQUAL_MEMORY(stcs, rig.uart.tx.data(), 3);
  TEST_ASSERT_EQUAL(0, rig.uart.releaseCount);
  TEST_ASSERT_EQUAL(0, rig.uart.pullUpOffCount);

  rig.uart.tx.clear();
  rig.uart.writeSizes.clear();
  rig.port.output.clear();
  scriptEnterFromRun(rig.uart);
  const uint8_t enter[] = {0x14};
  rig.send(2, enter, sizeof(enter));
  TEST_ASSERT_TRUE(rig.uart.writeSizes.size() > 0);
  TEST_ASSERT_EQUAL(2, rig.uart.writeSizes[0]);
  const uint8_t ldcs[] = {0x55, 0x8B};
  TEST_ASSERT_EQUAL_MEMORY(ldcs, rig.uart.tx.data(), 2);
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  const uint8_t forbidden[] = {0x55, 0xC2, 0x06};
  TEST_ASSERT_EQUAL(1, countOf(rig.uart.tx, guard, sizeof(guard)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, forbidden, sizeof(forbidden)));
}

void testUpdiEnterWhileDisconnectedAlsoBreaks()
{
  UpdiFixture rig;
  scriptEnterFromRun(rig.uart);
  const uint8_t body[] = {0x14};
  const std::vector<uint8_t> bytes = frame(1, body, sizeof(body));
  rig.port.feed(bytes.data(), bytes.size());
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.installed);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  rig.clock.advance(25);
  rig.programmer.update();
  rig.clock.advance(3);
  rig.programmer.update();
  rig.clock.advance(25);
  rig.programmer.update();
  TEST_ASSERT_TRUE(rig.uart.attached);
  TEST_ASSERT_EQUAL(6, rig.uart.gpio);
  TEST_ASSERT_EQUAL(115200, rig.uart.baud);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
}

void testUpdiReadsSignature()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  scriptEnterAfterSlowBoot(rig.uart);
  const uint8_t enter[] = {0x14};
  rig.send(2, enter, sizeof(enter));
  const uint8_t readReplies[] = {0x08, 0x40, 0x1E, 0x94, 0x22};
  rig.uart.reply(readReplies, sizeof(readReplies));
  uint8_t body[8] = {};
  body[0] = 0x05;
  body[2] = 3;
  body[6] = 0x00;
  body[7] = 0x11;
  rig.port.output.clear();
  rig.send(9, body, sizeof(body));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX16(9, sequence);
  const uint8_t expect[] = {0x82, 0x1E, 0x94, 0x22};
  TEST_ASSERT_EQUAL(4, answer.size());
  TEST_ASSERT_EQUAL_MEMORY(expect, answer.data(), 4);
}

void testUpdiChipEraseAndPageWrite()
{
  UpdiFixture rig;
  connect(rig);
  const uint8_t eraseReplies[] = {0x08, 0x08};
  rig.uart.reply(eraseReplies, sizeof(eraseReplies));
  const uint8_t erase[] = {0x34, 0x00};
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.send(4, erase, sizeof(erase));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kEraseKey, sizeof(kEraseKey)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
  const uint8_t keyStatus[] = {0x55, 0x87};
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  const uint8_t release[] = {0x55, 0xC8, 0x00};
  const uint8_t status[] = {0x55, 0x8B};
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  const uint8_t forbidden[] = {0x55, 0xC2, 0x06};
  TEST_ASSERT_TRUE(contains(rig.uart.tx, keyStatus, sizeof(keyStatus)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, release, sizeof(release)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, status, sizeof(status)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, guard, sizeof(guard)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, forbidden, sizeof(forbidden)));

  scriptPageWrite(rig.uart);
  uint8_t body[10 + 64] = {};
  body[0] = 0x04;
  body[1] = 0xC0;
  body[2] = 64;
  body[6] = 0x00;
  body[7] = 0x80;
  for (uint8_t index = 0; index < 64; ++index)
  {
    body[10 + index] = static_cast<uint8_t>(0xA0 + index);
  }
  rig.port.output.clear();
  rig.send(5, body, sizeof(body));
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  uint8_t expectPayload[64];
  for (uint8_t index = 0; index < 64; ++index)
  {
    expectPayload[index] = static_cast<uint8_t>(0xA0 + index);
  }
  TEST_ASSERT_TRUE(contains(rig.uart.tx, expectPayload, 64));
  const uint8_t commit[] = {0x55, 0x44, 0x00, 0x10, 0x01};
  TEST_ASSERT_TRUE(contains(rig.uart.tx, commit, sizeof(commit)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kEraseKey, sizeof(kEraseKey)));
}

void testUpdiBadCrcAndUnknownCommandStayInSession()
{
  FakeClock clock;
  EmptyModuleHostFixture fixture(clock);
  fixture.host.begin();
  FakeBytePort port;
  FakeHalfDuplexUart uart;
  FakeSpiMaster spi;
  UpdiProgrammer programmer(port, uart, clock);
  programmer.start(fixture.cs1, 6);
  ProgrammingSession session(fixture.host, port, clock, 60000, 1000);
  session.begin(programmer);
  TEST_ASSERT_EQUAL(0, spi.beginCount);

  const uint8_t command[] = {0x99};
  std::vector<uint8_t> bad = frame(8, command, sizeof(command));
  bad.back() ^= 0xFF;
  port.feed(bad.data(), bad.size());
  session.update();
  TEST_ASSERT_TRUE(session.active());
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX16(8, sequence);
  TEST_ASSERT_EQUAL_HEX8(0xA0, answer[0]);

  port.output.clear();
  const std::vector<uint8_t> unknown = frame(9, command, sizeof(command));
  port.feed(unknown.data(), unknown.size());
  session.update();
  TEST_ASSERT_TRUE(session.active());
  TEST_ASSERT_TRUE(parse(port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX16(9, sequence);
  TEST_ASSERT_EQUAL_HEX8(0xAA, answer[0]);
  TEST_ASSERT_EQUAL(0, spi.beginCount);
}

void testUpdiLeaveReleasesTheLineAndShutdownRestoresInput()
{
  UpdiFixture rig;
  connect(rig);
  TEST_ASSERT_TRUE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.attached);
  const uint8_t replies[] = {0x08, 0x00, 0x08};
  rig.uart.reply(replies, sizeof(replies));
  const uint8_t leave[] = {0x15};
  rig.port.output.clear();
  rig.send(1, leave, sizeof(leave));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_TRUE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);

  rig.programmer.shutdown();
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, rig.line.mode);
}

/**
 * Steps the 25/3/25 break. Early polls leave the pin unclaimed and
 * transmit nothing. The last poll loads STATUSA.
 *
 * @param rig Fixture whose link check has just started.
 * @param result Receives the finished measurement.
 * @return Nothing.
 */
void finishLinkCheck(UpdiFixture& rig, UpdiLinkCheck& result)
{
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_TRUE(rig.uart.installed);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  TEST_ASSERT_EQUAL(1, rig.uart.installCount);

  UpdiLinkCheck pending = {};
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  TEST_ASSERT_EQUAL(0, rig.uart.claimCount);

  rig.clock.advance(24);
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);

  rig.clock.advance(1);
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, rig.line.mode);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());

  rig.clock.advance(2);
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, rig.line.mode);
  TEST_ASSERT_EQUAL(0, rig.uart.claimCount);

  rig.clock.advance(1);
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());

  rig.clock.advance(24);
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(pending));
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_EQUAL(0, rig.uart.claimCount);

  rig.clock.advance(1);
  TEST_ASSERT_TRUE(rig.programmer.pollLinkCheck(result));
  TEST_ASSERT_FALSE(rig.programmer.pollLinkCheck(result));
}

void testUpdiLinkCheckSamplesLevelsThenLoadsStatus()
{
  UpdiFixture rig;
  const uint8_t hostByte = 0x1B;
  rig.port.feed(&hostByte, 1);
  rig.programmer.beginLinkCheck(rig.line, 6);
  rig.programmer.update();
  TEST_ASSERT_EQUAL(1, rig.port.input.size());
  TEST_ASSERT_EQUAL(PinMode::DigitalOutputOpenDrain, rig.line.mode);
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.line.pushPullUsed);
  rig.uart.reply(0x30);

  UpdiLinkCheck result = {};
  finishLinkCheck(rig, result);
  TEST_ASSERT_FALSE(result.drivenHigh);
  TEST_ASSERT_TRUE(result.releasedHigh);
  TEST_ASSERT_EQUAL(1, rig.uart.claimCount);
  TEST_ASSERT_FALSE(result.listenLow);
  TEST_ASSERT_EQUAL(0, result.pulseAtUs);
  TEST_ASSERT_EQUAL(0, result.pulseUs);
  TEST_ASSERT_EQUAL(0, result.edgeCount);
  TEST_ASSERT_EQUAL(static_cast<int>(UartListenFault::None),
                    static_cast<int>(result.uartFault));
  TEST_ASSERT_TRUE(result.echoSeen);
  TEST_ASSERT_TRUE(result.replied);
  TEST_ASSERT_EQUAL_HEX8(0x30, result.status);
  TEST_ASSERT_EQUAL(1, rig.uart.writeSizes.size());
  TEST_ASSERT_EQUAL(2, rig.uart.writeSizes[0]);
  const uint8_t ldcs[] = {0x55, 0x80};
  TEST_ASSERT_EQUAL(2, rig.uart.tx.size());
  TEST_ASSERT_EQUAL_MEMORY(ldcs, rig.uart.tx.data(), 2);
  TEST_ASSERT_EQUAL(6, rig.uart.gpio);
  TEST_ASSERT_EQUAL(115200, rig.uart.baud);
  TEST_ASSERT_EQUAL(1, rig.uart.releaseCount);
  TEST_ASSERT_EQUAL(1, rig.uart.pullUpOffCount);
  TEST_ASSERT_EQUAL(1, rig.uart.rearmCount);
  TEST_ASSERT_TRUE(rig.uart.pullUpEnabled);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, rig.line.mode);
  TEST_ASSERT_EQUAL(0, rig.port.output.size());
  TEST_ASSERT_EQUAL(1, rig.port.input.size());
}

void testUpdiLinkCheckWithNoStatusByteStillReleasesThePin()
{
  UpdiFixture rig;
  rig.programmer.beginLinkCheck(rig.line, 7);
  UpdiLinkCheck result = {};
  finishLinkCheck(rig, result);
  TEST_ASSERT_FALSE(result.drivenHigh);
  TEST_ASSERT_TRUE(result.releasedHigh);
  TEST_ASSERT_FALSE(result.listenLow);
  TEST_ASSERT_EQUAL(0, result.pulseAtUs);
  TEST_ASSERT_EQUAL(0, result.pulseUs);
  TEST_ASSERT_EQUAL(static_cast<int>(UartListenFault::None),
                    static_cast<int>(result.uartFault));
  TEST_ASSERT_TRUE(result.echoSeen);
  TEST_ASSERT_FALSE(result.replied);
  TEST_ASSERT_EQUAL(0, result.status);
  TEST_ASSERT_EQUAL(1, rig.uart.writeSizes.size());
  TEST_ASSERT_EQUAL(2, rig.uart.writeSizes[0]);
  const uint8_t ldcs[] = {0x55, 0x80};
  TEST_ASSERT_EQUAL_MEMORY(ldcs, rig.uart.tx.data(), 2);
  TEST_ASSERT_EQUAL(7, rig.uart.gpio);
  TEST_ASSERT_EQUAL(1, rig.uart.releaseCount);
  TEST_ASSERT_EQUAL(1, rig.uart.pullUpOffCount);
  TEST_ASSERT_EQUAL(1, rig.uart.rearmCount);
  TEST_ASSERT_TRUE(rig.uart.pullUpEnabled);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, rig.line.mode);
}

void testUpdiLinkCheckReportsADipAndAMissingEcho()
{
  UpdiFixture dipped;
  dipped.uart.scriptListenLow = true;
  dipped.uart.scriptPulseAtUs = 140;
  dipped.uart.scriptPulseUs = 9;
  dipped.uart.scriptEdgeCount = 4;
  dipped.uart.scriptWidthUs[0] = 2;
  dipped.uart.scriptWidthUs[1] = 2;
  dipped.uart.scriptWidthUs[2] = 2;
  dipped.uart.scriptWidthUs[3] = 2;
  dipped.uart.scriptListenFault = UartListenFault::Frame;
  dipped.programmer.beginLinkCheck(dipped.line, 6);
  UpdiLinkCheck low = {};
  finishLinkCheck(dipped, low);
  TEST_ASSERT_TRUE(low.listenLow);
  TEST_ASSERT_EQUAL(140, low.pulseAtUs);
  TEST_ASSERT_EQUAL(9, low.pulseUs);
  TEST_ASSERT_EQUAL(4, low.edgeCount);
  TEST_ASSERT_EQUAL(2, low.widthUs[0]);
  TEST_ASSERT_EQUAL(2, low.widthUs[3]);
  TEST_ASSERT_EQUAL(static_cast<int>(UartListenFault::Frame),
                    static_cast<int>(low.uartFault));
  TEST_ASSERT_TRUE(low.echoSeen);
  TEST_ASSERT_FALSE(low.replied);

  UpdiFixture silent;
  silent.uart.dropEcho = true;
  silent.programmer.beginLinkCheck(silent.line, 6);
  UpdiLinkCheck noEcho = {};
  finishLinkCheck(silent, noEcho);
  TEST_ASSERT_FALSE(noEcho.listenLow);
  TEST_ASSERT_EQUAL(0, noEcho.pulseAtUs);
  TEST_ASSERT_EQUAL(0, noEcho.pulseUs);
  TEST_ASSERT_EQUAL(static_cast<int>(UartListenFault::None),
                    static_cast<int>(noEcho.uartFault));
  TEST_ASSERT_FALSE(noEcho.echoSeen);
  TEST_ASSERT_FALSE(noEcho.replied);
  TEST_ASSERT_EQUAL(1, silent.uart.releaseCount);
  TEST_ASSERT_EQUAL(1, silent.uart.pullUpOffCount);
  TEST_ASSERT_EQUAL(1, silent.uart.rearmCount);
  const uint8_t ldcs[] = {0x55, 0x80};
  TEST_ASSERT_EQUAL_MEMORY(ldcs, silent.uart.tx.data(), 2);
}

void testUpdiLinkCheckSendsWhenTheGapStaysLow()
{
  UpdiFixture rig;
  rig.line.externalLevel = false;
  rig.uart.reply(0x30);
  rig.programmer.beginLinkCheck(rig.line, 6);
  UpdiLinkCheck result = {};
  finishLinkCheck(rig, result);
  TEST_ASSERT_FALSE(result.drivenHigh);
  TEST_ASSERT_FALSE(result.releasedHigh);
  TEST_ASSERT_EQUAL(1, rig.uart.claimCount);
  TEST_ASSERT_EQUAL(1, rig.uart.writeSizes.size());
  TEST_ASSERT_EQUAL(2, rig.uart.writeSizes[0]);
  const uint8_t ldcs[] = {0x55, 0x80};
  TEST_ASSERT_EQUAL_MEMORY(ldcs, rig.uart.tx.data(), 2);
  TEST_ASSERT_TRUE(result.echoSeen);
  TEST_ASSERT_TRUE(result.replied);
  TEST_ASSERT_EQUAL_HEX8(0x30, result.status);
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(PinMode::DigitalInput, rig.line.mode);
}

void testUpdiEnterWaitsForBootDone()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  scriptEnterAfterSlowBoot(rig.uart);
  const uint8_t enter[] = {0x14};
  rig.send(4, enter, sizeof(enter));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  TEST_ASSERT_EQUAL(1, countOf(rig.uart.tx, guard, sizeof(guard)));
}

void testUpdiEnterRetriesAMissedStatusByte()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.uart.writeSizes.clear();
  rig.uart.missReplyIndex = 1;
  rig.uart.plantLateOnMiss = true;
  rig.uart.lateOnMiss = 0x00;
  const uint8_t replies[] = {0x82, 0x02, 0x00, 0x08, 0x40, 0x40};
  rig.uart.reply(replies, sizeof(replies));
  const uint8_t enter[] = {0x14};
  rig.send(5, enter, sizeof(enter));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
  TEST_ASSERT_EQUAL(5, writesOf(rig.uart.writeSizes, 2));
}

void testUpdiEnterGivesUpWhenResetStaysClear()
{
  StepClock clock;
  FakeBytePort port;
  FakeHalfDuplexUart uart;
  FakeDigitalPin line;
  UpdiProgrammer programmer(port, uart, clock);
  programmer.start(line, 6);
  uint8_t descriptor[247] = {};
  descriptor[0] = 0x0C;
  descriptor[244] = 64;
  descriptor[246] = 32;
  const std::vector<uint8_t> setup = frame(7, descriptor, sizeof(descriptor));
  port.feed(setup.data(), setup.size());
  programmer.update();
  clock.advance(25);
  programmer.update();
  clock.advance(3);
  programmer.update();
  clock.advance(25);
  programmer.update();

  const uint8_t replies[] = {
    0x82,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uart.reply(replies, sizeof(replies));
  clock.autoStep = true;
  port.output.clear();
  uart.tx.clear();
  uart.writeSizes.clear();
  const uint8_t enter[] = {0x14};
  const std::vector<uint8_t> command = frame(3, enter, sizeof(enter));
  port.feed(command.data(), command.size());
  programmer.update();
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  TEST_ASSERT_TRUE(writesOf(uart.writeSizes, 2) > 5);
  TEST_ASSERT_FALSE(contains(uart.tx, kNvmKey, sizeof(kNvmKey)));
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  TEST_ASSERT_FALSE(contains(uart.tx, guard, sizeof(guard)));
}

void testUpdiEnterEchoMismatchDoesNotWait()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.uart.writeSizes.clear();
  rig.uart.dropEcho = true;
  const uint8_t replies[] = {0x55, 0x8B, 0x82, 0x00};
  rig.uart.reply(replies, sizeof(replies));
  const uint8_t enter[] = {0x14};
  rig.send(6, enter, sizeof(enter));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  TEST_ASSERT_EQUAL(1, writesOf(rig.uart.writeSizes, 2));
  TEST_ASSERT_EQUAL(1, writesOf(rig.uart.writeSizes, 3));
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  TEST_ASSERT_FALSE(contains(rig.uart.tx, guard, sizeof(guard)));
}

void testUpdiGoBeforeProgrammingLeavesThePinEnabled()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  const uint8_t go[] = {0x08};
  rig.send(8, go, sizeof(go));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
}

void testUpdiGoAfterEnterDisablesThePin()
{
  UpdiFixture rig;
  connect(rig);
  scriptEnterFromRun(rig.uart);
  const uint8_t enter[] = {0x14};
  rig.send(2, enter, sizeof(enter));
  rig.port.output.clear();
  rig.uart.tx.clear();
  const uint8_t go[] = {0x08};
  rig.send(8, go, sizeof(go));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  const uint8_t disable[] = {0x55, 0xC3, 0x04};
  TEST_ASSERT_EQUAL(3, rig.uart.tx.size());
  TEST_ASSERT_EQUAL_MEMORY(disable, rig.uart.tx.data(), 3);
}

void testUpdiSignOffBeforeProgrammingLeavesThePinEnabled()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  const uint8_t signOff[] = {0x00};
  rig.send(9, signOff, sizeof(signOff));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
}

void testUpdiChipEraseLockStaysSet()
{
  StepClock clock;
  clock.stepMs = 25;
  FakeBytePort port;
  FakeHalfDuplexUart uart;
  FakeDigitalPin line;
  UpdiProgrammer programmer(port, uart, clock);
  programmer.start(line, 6);
  connectStep(programmer, port, clock);

  uint8_t replies[17];
  replies[0] = 0x08;
  for (int index = 1; index < 17; ++index)
  {
    replies[index] = 0x01;
  }
  uart.reply(replies, sizeof(replies));
  clock.autoStep = true;
  port.output.clear();
  uart.tx.clear();
  uart.writeSizes.clear();
  const uint8_t erase[] = {0x34, 0x00};
  const std::vector<uint8_t> command = frame(4, erase, sizeof(erase));
  port.feed(command.data(), command.size());
  programmer.update();
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  TEST_ASSERT_TRUE(contains(uart.tx, kEraseKey, sizeof(kEraseKey)));
  TEST_ASSERT_FALSE(contains(uart.tx, kNvmKey, sizeof(kNvmKey)));
  const uint8_t keyStatus[] = {0x55, 0x87};
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  const uint8_t release[] = {0x55, 0xC8, 0x00};
  const uint8_t status[] = {0x55, 0x8B};
  TEST_ASSERT_EQUAL(1, countOf(uart.tx, keyStatus, sizeof(keyStatus)));
  TEST_ASSERT_TRUE(contains(uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_TRUE(contains(uart.tx, release, sizeof(release)));
  TEST_ASSERT_EQUAL(7, countOf(uart.tx, status, sizeof(status)));
}

void testUpdiChipEraseStatusZeroIsOk()
{
  UpdiFixture rig;
  connect(rig);
  const uint8_t replies[] = {0x08, 0x00};
  rig.uart.reply(replies, sizeof(replies));
  const uint8_t erase[] = {0x34, 0x00};
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.send(4, erase, sizeof(erase));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  const uint8_t keyStatus[] = {0x55, 0x87};
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  const uint8_t release[] = {0x55, 0xC8, 0x00};
  const uint8_t status[] = {0x55, 0x8B};
  TEST_ASSERT_EQUAL(1, countOf(rig.uart.tx, keyStatus, sizeof(keyStatus)));
  TEST_ASSERT_EQUAL(1, countOf(rig.uart.tx, status, sizeof(status)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, release, sizeof(release)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
}

void testUpdiChipEraseKeyStatusRejected()
{
  UpdiFixture rig;
  connect(rig);
  rig.uart.reply(0x07);
  const uint8_t erase[] = {0x34, 0x00};
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.send(4, erase, sizeof(erase));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kEraseKey, sizeof(kEraseKey)));
  const uint8_t keyStatus[] = {0x55, 0x87};
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  const uint8_t release[] = {0x55, 0xC8, 0x00};
  TEST_ASSERT_TRUE(contains(rig.uart.tx, keyStatus, sizeof(keyStatus)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, release, sizeof(release)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
}

void testUpdiChipEraseKeyEchoMismatch()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.uart.writeSizes.clear();
  rig.uart.dropEcho = true;
  const uint8_t erase[] = {0x34, 0x00};
  rig.send(4, erase, sizeof(erase));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  TEST_ASSERT_EQUAL(1, writesOf(rig.uart.writeSizes, 10));
  TEST_ASSERT_EQUAL(0, writesOf(rig.uart.writeSizes, 3));
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  TEST_ASSERT_FALSE(contains(rig.uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
}

void testUpdiChipEraseRetriesAMissedLockByte()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  rig.uart.tx.clear();
  rig.uart.missReplyIndex = 1;
  rig.uart.plantLateOnMiss = true;
  rig.uart.lateOnMiss = 0x01;
  const uint8_t replies[] = {0x08, 0x00};
  rig.uart.reply(replies, sizeof(replies));
  const uint8_t erase[] = {0x34, 0x00};
  rig.send(4, erase, sizeof(erase));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  const uint8_t status[] = {0x55, 0x8B};
  const uint8_t hold[] = {0x55, 0xC8, 0x59};
  const uint8_t release[] = {0x55, 0xC8, 0x00};
  TEST_ASSERT_EQUAL(2, countOf(rig.uart.tx, status, sizeof(status)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, hold, sizeof(hold)));
  TEST_ASSERT_TRUE(contains(rig.uart.tx, release, sizeof(release)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
}

void testUpdiChipEraseAfterEnterDoesNotDisableThePin()
{
  UpdiFixture rig;
  connect(rig);
  rig.port.output.clear();
  scriptEnterFromRun(rig.uart);
  const uint8_t enter[] = {0x14};
  rig.send(2, enter, sizeof(enter));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  const uint8_t guard[] = {0x55, 0xC2, 0x03};
  TEST_ASSERT_EQUAL(1, countOf(rig.uart.tx, guard, sizeof(guard)));

  rig.uart.tx.clear();
  const uint8_t eraseReplies[] = {0x08, 0x00};
  rig.uart.reply(eraseReplies, sizeof(eraseReplies));
  const uint8_t erase[] = {0x34, 0x00};
  rig.port.output.clear();
  rig.send(4, erase, sizeof(erase));
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kEraseKey, sizeof(kEraseKey)));
  TEST_ASSERT_FALSE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));

  rig.port.output.clear();
  rig.uart.tx.clear();
  const uint8_t go[] = {0x08};
  rig.send(8, go, sizeof(go));
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
}

void testUpdiNvmWaitGivesUpAfterTwentyMilliseconds()
{
  StepClock clock;
  clock.stepMs = 5;
  FakeBytePort port;
  FakeHalfDuplexUart uart;
  FakeDigitalPin line;
  UpdiProgrammer programmer(port, uart, clock);
  programmer.start(line, 6);
  connectStep(programmer, port, clock);
  clock.autoStep = true;
  const uint8_t replies[] = {0x08, 0x40, 0x00, 0x00, 0x01, 0x01, 0x01};
  uart.reply(replies, sizeof(replies));
  uint8_t body[10 + 64] = {};
  body[0] = 0x04;
  body[1] = 0xC0;
  body[2] = 64;
  body[6] = 0x00;
  body[7] = 0x80;
  for (uint8_t index = 0; index < 64; ++index)
  {
    body[10 + index] = static_cast<uint8_t>(0xA0 + index);
  }
  port.output.clear();
  uart.tx.clear();
  const std::vector<uint8_t> command = frame(5, body, sizeof(body));
  port.feed(command.data(), command.size());
  programmer.update();
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(port.output, sequence, answer));
  TEST_ASSERT_EQUAL_HEX8(0xAB, answer[0]);
  const uint8_t status[] = {0x55, 0x04, 0x02, 0x10};
  const uint8_t commit[] = {0x55, 0x44, 0x00, 0x10, 0x01};
  TEST_ASSERT_EQUAL(3, countOf(uart.tx, status, sizeof(status)));
  TEST_ASSERT_FALSE(contains(uart.tx, body + 10, 1));
  TEST_ASSERT_FALSE(contains(uart.tx, commit, sizeof(commit)));
}

void testUpdiSessionDoesNotUseSpi()
{
  UpdiFixture rig;
  connect(rig);
  const uint8_t body[] = {0x01};
  rig.send(1, body, sizeof(body));
  FakeSpiMaster spi;
  TEST_ASSERT_EQUAL(0, spi.beginCount);
  TEST_ASSERT_TRUE(rig.uart.attached);
}


// ========== Runner ==========

/**
 * Registers the jtag2updi cases.
 *
 * @return Nothing.
 */
void runUpdiTests()
{
  UnitySetTestFile(__FILE__);
  RUN_TEST(testCrc16JtagKnownVector);
  RUN_TEST(testUpdiSignOnReturnsJtagiceBody);
  RUN_TEST(testUpdiDescriptorDoubleBreakThenControlA);
  RUN_TEST(testUpdiGroupsInstructionIntoOneWrite);
  RUN_TEST(testUpdiEnterWhileDisconnectedAlsoBreaks);
  RUN_TEST(testUpdiReadsSignature);
  RUN_TEST(testUpdiEnterWaitsForBootDone);
  RUN_TEST(testUpdiEnterRetriesAMissedStatusByte);
  RUN_TEST(testUpdiEnterGivesUpWhenResetStaysClear);
  RUN_TEST(testUpdiEnterEchoMismatchDoesNotWait);
  RUN_TEST(testUpdiGoBeforeProgrammingLeavesThePinEnabled);
  RUN_TEST(testUpdiGoAfterEnterDisablesThePin);
  RUN_TEST(testUpdiSignOffBeforeProgrammingLeavesThePinEnabled);
  RUN_TEST(testUpdiChipEraseAndPageWrite);
  RUN_TEST(testUpdiChipEraseLockStaysSet);
  RUN_TEST(testUpdiChipEraseStatusZeroIsOk);
  RUN_TEST(testUpdiChipEraseKeyStatusRejected);
  RUN_TEST(testUpdiChipEraseKeyEchoMismatch);
  RUN_TEST(testUpdiChipEraseRetriesAMissedLockByte);
  RUN_TEST(testUpdiChipEraseAfterEnterDoesNotDisableThePin);
  RUN_TEST(testUpdiNvmWaitGivesUpAfterTwentyMilliseconds);
  RUN_TEST(testUpdiBadCrcAndUnknownCommandStayInSession);
  RUN_TEST(testUpdiLeaveReleasesTheLineAndShutdownRestoresInput);
  RUN_TEST(testUpdiLinkCheckSamplesLevelsThenLoadsStatus);
  RUN_TEST(testUpdiLinkCheckWithNoStatusByteStillReleasesThePin);
  RUN_TEST(testUpdiLinkCheckReportsADipAndAMissingEcho);
  RUN_TEST(testUpdiLinkCheckSendsWhenTheGapStaysLow);
  RUN_TEST(testUpdiSessionDoesNotUseSpi);
}

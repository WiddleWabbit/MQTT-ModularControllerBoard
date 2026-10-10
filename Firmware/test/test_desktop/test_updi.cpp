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
 * Connects with a 64-byte flash page and runs the 25/1/25 break.
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
  rig.clock.advance(1);
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
  TEST_ASSERT_EQUAL_HEX16(0x4B37, crc16Jtag(text, sizeof(text)));
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
  TEST_ASSERT_EQUAL(0, rig.uart.attachCount);
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
  TEST_ASSERT_FALSE(rig.uart.attached);
  TEST_ASSERT_EQUAL(0, rig.port.output.size());

  rig.clock.advance(24);
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);

  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_TRUE(rig.line.level);
  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  rig.clock.advance(24);
  rig.programmer.update();
  TEST_ASSERT_FALSE(rig.line.level);
  TEST_ASSERT_FALSE(rig.uart.attached);

  rig.clock.advance(1);
  rig.programmer.update();
  TEST_ASSERT_TRUE(rig.line.level);
  TEST_ASSERT_TRUE(rig.uart.attached);
  TEST_ASSERT_EQUAL(6, rig.uart.gpio);
  TEST_ASSERT_EQUAL(115200, rig.uart.baud);
  const uint8_t stcs[] = {0x55, 0xC2, 0x06};
  TEST_ASSERT_TRUE(contains(rig.uart.tx, stcs, sizeof(stcs)));
  uint16_t sequence = 0;
  std::vector<uint8_t> answer;
  TEST_ASSERT_TRUE(parse(rig.port.output, sequence, answer));
  TEST_ASSERT_EQUAL(1, answer.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, answer[0]);
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
  TEST_ASSERT_EQUAL(0, rig.uart.tx.size());
  rig.clock.advance(25);
  rig.programmer.update();
  rig.clock.advance(1);
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
  scriptEnterFromRun(rig.uart);
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
  const uint8_t eraseReplies[] = {0x08, 0x82, 0x08, 0x00, 0x08, 0x40, 0x40};
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
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));

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
  TEST_ASSERT_TRUE(contains(rig.uart.tx, kNvmKey, sizeof(kNvmKey)));
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

void testUpdiLeaveReleasesTheLineAndShutdownRestoresPullup()
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
  TEST_ASSERT_EQUAL(PinMode::DigitalInputPullup, rig.line.mode);
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
  RUN_TEST(testUpdiEnterWhileDisconnectedAlsoBreaks);
  RUN_TEST(testUpdiReadsSignature);
  RUN_TEST(testUpdiChipEraseAndPageWrite);
  RUN_TEST(testUpdiBadCrcAndUnknownCommandStayInSession);
  RUN_TEST(testUpdiLeaveReleasesTheLineAndShutdownRestoresPullup);
  RUN_TEST(testUpdiSessionDoesNotUseSpi);
}

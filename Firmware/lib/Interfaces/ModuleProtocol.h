#pragma once

#include <cstddef>
#include <cstdint>

namespace module_protocol
{
// ---------- Addressing (7-bit) ----------
constexpr uint8_t kUnconfiguredAddress = 0x0A;
constexpr uint8_t kSlotAddressBase = 0x10;
constexpr uint8_t kSlotCount = 4;
constexpr uint8_t kMinAssignedAddress = 0x10;
constexpr uint8_t kMaxAssignedAddress = 0x6F;
constexpr uint8_t kGeneralCallAddress = 0x00;

// ---------- Frame ----------
// byte0 length = bytes after byte0 including CRC (= 2 + payloadLen)
// byte1 command (request) or status (response)
// bytes 2..length-1 payload (big-endian)
// byte length = CRC-8
constexpr uint8_t kMaxPayload = 16;
constexpr uint8_t kMaxFrameBytes = 19;
constexpr uint8_t kMinFrameBytes = 3;
constexpr uint8_t kProtocolVersion = 1;

// Host always writeReads kMaxFrameBytes. Module clocks the real frame
// then 0xFF pad (SDA released). Let len = rx[0]. CRC-8/SMBus over
// rx[0 .. len-1], compare to rx[len]; ignore rx[len+1 .. 18].
// Host inspects only the first len+1 bytes of the 19-byte buffer.

// ---------- CRC-8/SMBus (PEC) ----------
// width=8 poly=0x07 init=0x00 refin=false refout=false xorout=0x00
constexpr uint8_t kCrc8Poly = 0x07;
constexpr uint8_t kCrc8Init = 0x00;

/**
 * Computes CRC-8/SMBus over a byte span.
 *
 * @param data Bytes to hash.
 * @param length Number of bytes in data.
 * @return CRC-8 value.
 */
inline uint8_t crc8Smbus(const uint8_t* data, size_t length)
{
  uint8_t crc = kCrc8Init;
  for (size_t i = 0; i < length; ++i)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if ((crc & 0x80) != 0)
      {
        crc = static_cast<uint8_t>((crc << 1) ^ kCrc8Poly);
      }
      else
      {
        crc = static_cast<uint8_t>(crc << 1);
      }
    }
  }
  return crc;
}

// ---------- Commands ----------
constexpr uint8_t kCmdPing = 0x01;
constexpr uint8_t kCmdGetIdentity = 0x02;
constexpr uint8_t kCmdSetAddress = 0x03;
constexpr uint8_t kCmdEcho = 0x40;

// Sensor module (type 0x0200) commands. Other types are not required
// to implement these. Requests for connected/reading carry a 0-based
// sensor index. Responses are big-endian.
constexpr uint8_t kCmdGetSensorCount = 0x41;
constexpr uint8_t kCmdGetSensorConnected = 0x42;
constexpr uint8_t kCmdGetSensorReading = 0x43;
constexpr uint8_t kMaxSensorsPerModule = 16;
constexpr uint8_t kSensorCountPayloadLen = 1;
constexpr uint8_t kSensorConnectedPayloadLen = 2;
constexpr uint8_t kSensorReadingPayloadLen = 6;

// SET_ADDRESS on-wire size. length field = 3 (2 + 1-byte payload);
// total frame = 4. Do not count the I2C 7-bit address as a frame byte.
constexpr uint8_t kSetAddressPayloadLen = 1;
constexpr uint8_t kSetAddressLengthField = 3;
constexpr uint8_t kSetAddressFrameBytes = 4;

// GET_IDENTITY Ok body. length field = 7; total frame = 8 inside the
// 19-byte padded read. Status Ok with any other length is BadFrame.
constexpr uint8_t kIdentityPayloadLen = 5;
constexpr uint8_t kIdentityLengthField = 7;

// ---------- Status (response byte 1) ----------
constexpr uint8_t kStatusOk = 0x00;
constexpr uint8_t kStatusBadCrc = 0x01;
constexpr uint8_t kStatusUnknownCmd = 0x02;
constexpr uint8_t kStatusBadLength = 0x03;
constexpr uint8_t kStatusBusy = 0x04;
constexpr uint8_t kStatusUnsupported = 0x05;

// ---------- Types ----------
constexpr uint16_t kTypeIdentityEcho = 0x0001;
constexpr uint16_t kTypeSensorModule = 0x0200;

// ---------- Packed payloads (wire order = struct order, big-endian) ----------
struct IdentityPayload
{
  uint8_t typeIdHi;
  uint8_t typeIdLo;
  uint8_t protocolVersion;
  uint8_t firmwareVersionHi;
  uint8_t firmwareVersionLo;
};

/**
 * Reads the 16-bit type id from an identity payload.
 *
 * @param p Identity payload.
 * @return Type id.
 */
inline uint16_t identityTypeId(const IdentityPayload& p)
{
  return static_cast<uint16_t>((p.typeIdHi << 8) | p.typeIdLo);
}

/**
 * Reads the 16-bit firmware version from an identity payload.
 *
 * @param p Identity payload.
 * @return Firmware version.
 */
inline uint16_t identityFirmwareVersion(const IdentityPayload& p)
{
  return static_cast<uint16_t>((p.firmwareVersionHi << 8) |
                               p.firmwareVersionLo);
}

struct SetAddressPayload
{
  uint8_t newAddress7bit;
};

/**
 * Writes a signed 32-bit value in big-endian wire order.
 *
 * @param dest Four-byte destination.
 * @param value Value to write.
 * @return Nothing.
 */
inline void writeInt32Be(uint8_t* dest, int32_t value)
{
  const uint32_t bits = static_cast<uint32_t>(value);
  dest[0] = static_cast<uint8_t>((bits >> 24) & 0xFF);
  dest[1] = static_cast<uint8_t>((bits >> 16) & 0xFF);
  dest[2] = static_cast<uint8_t>((bits >> 8) & 0xFF);
  dest[3] = static_cast<uint8_t>(bits & 0xFF);
}

/**
 * Reads a signed 32-bit value from big-endian wire order.
 *
 * @param data Four-byte source.
 * @return Decoded value.
 */
inline int32_t readInt32Be(const uint8_t* data)
{
  const uint32_t bits = (static_cast<uint32_t>(data[0]) << 24) |
                        (static_cast<uint32_t>(data[1]) << 16) |
                        (static_cast<uint32_t>(data[2]) << 8) |
                        static_cast<uint32_t>(data[3]);
  return static_cast<int32_t>(bits);
}

// SET_ADDRESS request layout (host write, no read):
//   tx[0] = kSetAddressLengthField  (0x03)
//   tx[1] = kCmdSetAddress
//   tx[2] = newAddress7bit
//   tx[3] = crc8Smbus(tx, 3)
//   write(0x0A, tx, kSetAddressFrameBytes) + STOP
// Not 5 bytes. The I2C address is not part of the frame.

constexpr uint32_t kModGateMaxMs = 5;
constexpr uint32_t kClockStretchMaxMs = 40;

/**
 * Returns the slot-derived 7-bit address for a firmware slot index.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Address 0x10..0x13.
 */
inline uint8_t slotAddress(uint8_t slotIndex)
{
  return static_cast<uint8_t>(kSlotAddressBase + slotIndex);
}

/**
 * Reports whether a 7-bit address is a legal assigned module address.
 *
 * @param address Candidate 7-bit address.
 * @return True when the address is in 0x10..0x6F and not 0x0A.
 */
inline bool isAssignableAddress(uint8_t address)
{
  return address >= kMinAssignedAddress && address <= kMaxAssignedAddress &&
         address != kUnconfiguredAddress;
}
}

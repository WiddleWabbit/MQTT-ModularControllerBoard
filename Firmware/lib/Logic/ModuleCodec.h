#pragma once

#include <cstddef>
#include <cstdint>

#include "ModuleProtocol.h"

/**
 * Result of decoding a padded module response frame.
 */
enum class ModuleDecodeStatus : uint8_t
{
  Ok,
  BadLength,
  BadCrc
};

/**
 * Decoded command/status and payload from a 19-byte read buffer.
 */
struct ModuleDecodedFrame
{
  ModuleDecodeStatus decodeStatus;
  uint8_t statusByte;
  uint8_t payload[module_protocol::kMaxPayload];
  uint8_t payloadLen;
};

/**
 * Encodes and decodes module protocol frames. Host-side only; module
 * firmware copies ModuleProtocol.h instead of this codec.
 */
class ModuleCodec
{
public:
  /**
   * Encodes a command frame into out. Returns on-wire size including CRC.
   *
   * @param cmd Command byte.
   * @param payload Payload bytes, or nullptr when payloadLen is 0.
   * @param payloadLen Payload length, 0..kMaxPayload.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Encoded size, or 0 when the arguments are invalid.
   */
  static size_t encodeCommand(uint8_t cmd, const uint8_t* payload,
                              size_t payloadLen, uint8_t* out, size_t outCap);

  /**
   * Encodes a PING request.
   *
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Encoded size.
   */
  static size_t encodePing(uint8_t* out, size_t outCap);

  /**
   * Encodes a GET_IDENTITY request.
   *
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Encoded size.
   */
  static size_t encodeGetIdentity(uint8_t* out, size_t outCap);

  /**
   * Encodes a 4-byte SET_ADDRESS request.
   *
   * @param newAddr Assigned 7-bit address.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Encoded size (4) or 0.
   */
  static size_t encodeSetAddress(uint8_t newAddr, uint8_t* out, size_t outCap);

  /**
   * Encodes an ECHO request.
   *
   * @param payload Bytes to echo.
   * @param payloadLen Payload length, 0..kMaxPayload.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Encoded size or 0.
   */
  static size_t encodeEcho(const uint8_t* payload, size_t payloadLen,
                           uint8_t* out, size_t outCap);

  /**
   * Decodes a padded read buffer. CRC-8/SMBus over rx[0 .. len-1],
   * compared to rx[len]. Pad after the CRC is ignored.
   *
   * @param rx Received bytes.
   * @param rxLen Number of bytes in rx (typically kMaxFrameBytes).
   * @return Decoded frame and status.
   */
  static ModuleDecodedFrame decode(const uint8_t* rx, size_t rxLen);
};

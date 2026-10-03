#include "ModuleCodec.h"

// ========== Public API ==========

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
size_t ModuleCodec::encodeCommand(uint8_t cmd, const uint8_t* payload,
                                  size_t payloadLen, uint8_t* out,
                                  size_t outCap)
{
  if (out == nullptr || payloadLen > module_protocol::kMaxPayload)
  {
    return 0;
  }
  if (payloadLen > 0 && payload == nullptr)
  {
    return 0;
  }

  const size_t frameSize = 2 + payloadLen + 1;
  if (outCap < frameSize)
  {
    return 0;
  }

  const uint8_t lengthField = static_cast<uint8_t>(2 + payloadLen);
  out[0] = lengthField;
  out[1] = cmd;
  for (size_t i = 0; i < payloadLen; ++i)
  {
    out[2 + i] = payload[i];
  }
  out[lengthField] = module_protocol::crc8Smbus(out, lengthField);
  return frameSize;
}

/**
 * Encodes a PING request.
 *
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return Encoded size.
 */
size_t ModuleCodec::encodePing(uint8_t* out, size_t outCap)
{
  return encodeCommand(module_protocol::kCmdPing, nullptr, 0, out, outCap);
}

/**
 * Encodes a GET_IDENTITY request.
 *
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return Encoded size.
 */
size_t ModuleCodec::encodeGetIdentity(uint8_t* out, size_t outCap)
{
  return encodeCommand(module_protocol::kCmdGetIdentity, nullptr, 0, out,
                       outCap);
}

/**
 * Encodes a 4-byte SET_ADDRESS request.
 *
 * @param newAddr Assigned 7-bit address.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return Encoded size (4) or 0.
 */
size_t ModuleCodec::encodeSetAddress(uint8_t newAddr, uint8_t* out,
                                     size_t outCap)
{
  const uint8_t payload[1] = {newAddr};
  return encodeCommand(module_protocol::kCmdSetAddress, payload, 1, out,
                       outCap);
}

/**
 * Encodes an ECHO request.
 *
 * @param payload Bytes to echo.
 * @param payloadLen Payload length, 0..kMaxPayload.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return Encoded size or 0.
 */
size_t ModuleCodec::encodeEcho(const uint8_t* payload, size_t payloadLen,
                               uint8_t* out, size_t outCap)
{
  return encodeCommand(module_protocol::kCmdEcho, payload, payloadLen, out,
                       outCap);
}

/**
 * Decodes a padded read buffer. CRC-8/SMBus over rx[0 .. len-1],
 * compared to rx[len]. Pad after the CRC is ignored.
 *
 * @param rx Received bytes.
 * @param rxLen Number of bytes in rx (typically kMaxFrameBytes).
 * @return Decoded frame and status.
 */
ModuleDecodedFrame ModuleCodec::decode(const uint8_t* rx, size_t rxLen)
{
  ModuleDecodedFrame decoded{};
  decoded.decodeStatus = ModuleDecodeStatus::BadLength;
  decoded.statusByte = 0;
  decoded.payloadLen = 0;

  if (rx == nullptr || rxLen < module_protocol::kMinFrameBytes)
  {
    return decoded;
  }

  const uint8_t len = rx[0];
  if (len < 2 || len > 18)
  {
    return decoded;
  }
  if (rxLen < static_cast<size_t>(len) + 1U)
  {
    return decoded;
  }

  const uint8_t crc = module_protocol::crc8Smbus(rx, len);
  if (crc != rx[len])
  {
    decoded.decodeStatus = ModuleDecodeStatus::BadCrc;
    return decoded;
  }

  decoded.decodeStatus = ModuleDecodeStatus::Ok;
  decoded.statusByte = rx[1];
  decoded.payloadLen = static_cast<uint8_t>(len - 2);
  for (uint8_t i = 0; i < decoded.payloadLen; ++i)
  {
    decoded.payload[i] = rx[2 + i];
  }
  return decoded;
}

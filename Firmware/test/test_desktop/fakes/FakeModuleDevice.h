#pragma once

#include <cstdint>
#include <cstring>

#include "FakeClock.h"
#include "FakeDigitalPin.h"
#include "I2cMaster.h"
#include "ModuleProtocol.h"

/**
 * Simulated daughter MCU that gates 0x0A on MOD and speaks the protocol.
 */
class FakeModuleDevice
{
public:
  FakeClock& clock;
  FakeDigitalPin& mod;
  uint16_t typeId = module_protocol::kTypeIdentityEcho;
  uint8_t protocolVersion = module_protocol::kProtocolVersion;
  uint16_t firmwareVersion = 0x0001;
  uint32_t modAckDelayMs = 0;
  bool ignoreMod = false;
  bool forceBusy = false;
  bool forceBusyOnIdentity = false;
  bool forceBadCrc = false;
  bool shortSensorPayload = false;
  uint8_t sensorCount = 0;
  bool sensorConnected[module_protocol::kMaxSensorsPerModule] = {};
  int32_t sensorValue[module_protocol::kMaxSensorsPerModule] = {};
  uint8_t identityLengthField = module_protocol::kIdentityLengthField;
  int nackRemaining = 0;
  bool nackIdentity = false;
  bool nackAssignedPing = false;
  bool assigned = false;
  uint8_t assignedAddress = 0;

  /**
   * Creates a module bound to a MOD pin and clock.
   *
   * @param clockRef Monotonic clock for MOD settle delay.
   * @param modPin Host MOD pin for this slot.
   */
  FakeModuleDevice(FakeClock& clockRef, FakeDigitalPin& modPin)
    : clock(clockRef),
      mod(modPin)
  {
  }

  /**
   * Returns the module to a MOD-gated unconfigured address.
   *
   * @return Nothing.
   */
  void resetToUnconfigured()
  {
    assigned = false;
    assignedAddress = 0;
    _modLowSince = 0;
    _sawModLow = false;
  }

  /**
   * Reports whether this device ACKs a 7-bit address now.
   *
   * @param address 7-bit address.
   * @return True when the device ACKs.
   */
  bool acks(uint8_t address)
  {
    _syncMod();
    if (assigned)
    {
      return address == assignedAddress;
    }
    if (address != module_protocol::kUnconfiguredAddress)
    {
      return false;
    }
    if (ignoreMod)
    {
      return true;
    }
    return _selectedLongEnough();
  }

  /**
   * Consumes a write. Commits SET_ADDRESS only on a 4-byte STOP frame.
   *
   * @param address 7-bit address.
   * @param data Written bytes.
   * @param length Byte count.
   * @param issuedStop True when the master issued STOP.
   * @return Transaction status.
   */
  I2cTxnStatus onWrite(uint8_t address, const uint8_t* data, size_t length,
                       bool issuedStop)
  {
    if (!acks(address))
    {
      if (nackRemaining > 0)
      {
        nackRemaining--;
      }
      return I2cTxnStatus::Nack;
    }
    if (nackRemaining > 0)
    {
      nackRemaining--;
      return I2cTxnStatus::Nack;
    }
    if (address == module_protocol::kUnconfiguredAddress &&
        length == module_protocol::kSetAddressFrameBytes && issuedStop &&
        data != nullptr && data[0] == module_protocol::kSetAddressLengthField &&
        data[1] == module_protocol::kCmdSetAddress)
    {
      const uint8_t crc = module_protocol::crc8Smbus(data, 3);
      const uint8_t newAddr = data[2];
      if (crc == data[3] && module_protocol::isAssignableAddress(newAddr))
      {
        assigned = true;
        assignedAddress = newAddr;
      }
    }
    return I2cTxnStatus::Ok;
  }

  /**
   * Fills a 19-byte padded response for a writeRead.
   *
   * @param address 7-bit address.
   * @param tx Written command frame.
   * @param txLen Written length.
   * @param rx Destination buffer.
   * @param rxLen Destination length, expected 19.
   * @return Transaction status.
   */
  I2cTxnStatus onWriteRead(uint8_t address, const uint8_t* tx, size_t txLen,
                           uint8_t* rx, size_t rxLen)
  {
    if (!acks(address))
    {
      if (nackRemaining > 0)
      {
        nackRemaining--;
      }
      return I2cTxnStatus::Nack;
    }
    if (nackRemaining > 0)
    {
      nackRemaining--;
      return I2cTxnStatus::Nack;
    }
    if (rx == nullptr || rxLen < module_protocol::kMaxFrameBytes ||
        tx == nullptr || txLen < module_protocol::kMinFrameBytes)
    {
      return I2cTxnStatus::BusError;
    }

    for (size_t i = 0; i < rxLen; ++i)
    {
      rx[i] = 0xFF;
    }

    const uint8_t cmd = tx[1];
    if (nackIdentity && cmd == module_protocol::kCmdGetIdentity)
    {
      return I2cTxnStatus::Nack;
    }
    if (nackAssignedPing && cmd == module_protocol::kCmdPing && assigned &&
        address == assignedAddress)
    {
      return I2cTxnStatus::Nack;
    }
    if (forceBusy ||
        (forceBusyOnIdentity && cmd == module_protocol::kCmdGetIdentity))
    {
      _writeStatus(rx, module_protocol::kStatusBusy, nullptr, 0);
      return I2cTxnStatus::Ok;
    }

    if (cmd == module_protocol::kCmdPing)
    {
      _writeStatus(rx, module_protocol::kStatusOk, nullptr, 0);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdGetIdentity)
    {
      uint8_t payload[5] = {
        static_cast<uint8_t>(typeId >> 8),
        static_cast<uint8_t>(typeId & 0xFF),
        protocolVersion,
        static_cast<uint8_t>(firmwareVersion >> 8),
        static_cast<uint8_t>(firmwareVersion & 0xFF),
      };
      const uint8_t payloadLen = static_cast<uint8_t>(
          identityLengthField >= 2 ? identityLengthField - 2 : 0);
      _writeStatus(rx, module_protocol::kStatusOk, payload, payloadLen);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdEcho)
    {
      const uint8_t payloadLen = static_cast<uint8_t>(tx[0] - 2);
      _writeStatus(rx, module_protocol::kStatusOk, tx + 2, payloadLen);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdGetSensorCount ||
        cmd == module_protocol::kCmdGetSensorConnected ||
        cmd == module_protocol::kCmdGetSensorReading)
    {
      if (shortSensorPayload)
      {
        _writeStatus(rx, module_protocol::kStatusOk, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetSensorCount)
      {
        _writeStatus(rx, module_protocol::kStatusOk, &sensorCount, 1);
        return I2cTxnStatus::Ok;
      }
      if (tx[0] < 3)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      const uint8_t index = tx[2];
      if (index >= sensorCount ||
          index >= module_protocol::kMaxSensorsPerModule)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetSensorConnected)
      {
        const uint8_t body[2] = {
          index, static_cast<uint8_t>(sensorConnected[index] ? 1 : 0)};
        _writeStatus(rx, module_protocol::kStatusOk, body, 2);
        return I2cTxnStatus::Ok;
      }
      uint8_t body[module_protocol::kSensorReadingPayloadLen];
      body[0] = index;
      body[1] = static_cast<uint8_t>(sensorConnected[index] ? 1 : 0);
      module_protocol::writeInt32Be(body + 2, sensorValue[index]);
      _writeStatus(rx, module_protocol::kStatusOk, body,
                   module_protocol::kSensorReadingPayloadLen);
      return I2cTxnStatus::Ok;
    }

    _writeStatus(rx, module_protocol::kStatusUnknownCmd, nullptr, 0);
    return I2cTxnStatus::Ok;
  }

private:
  uint32_t _modLowSince = 0;
  bool _sawModLow = false;

  /**
   * Tracks when MOD went open-drain LOW.
   *
   * @return Nothing.
   */
  void _syncMod()
  {
    const bool selected = ignoreMod ||
                          (mod.mode == PinMode::DigitalOutputOpenDrain &&
                           !mod.level);
    if (selected)
    {
      if (!_sawModLow)
      {
        _modLowSince = clock.millis();
        _sawModLow = true;
      }
    }
    else
    {
      _sawModLow = false;
    }
  }

  /**
   * Reports whether MOD has been low long enough to ACK 0x0A.
   *
   * @return True when the gate delay has elapsed.
   */
  bool _selectedLongEnough() const
  {
    if (!_sawModLow)
    {
      return false;
    }
    if (mod.mode != PinMode::DigitalOutputOpenDrain || mod.level)
    {
      return false;
    }
    return static_cast<uint32_t>(clock.millis() - _modLowSince) >=
           modAckDelayMs;
  }

  /**
   * Writes a status frame plus 0xFF pad already filled in rx.
   *
   * @param rx Destination.
   * @param status Status byte.
   * @param payload Payload bytes.
   * @param payloadLen Payload length.
   * @return Nothing.
   */
  void _writeStatus(uint8_t* rx, uint8_t status, const uint8_t* payload,
                    uint8_t payloadLen)
  {
    const uint8_t lengthField = static_cast<uint8_t>(2 + payloadLen);
    rx[0] = lengthField;
    rx[1] = status;
    for (uint8_t i = 0; i < payloadLen; ++i)
    {
      rx[2 + i] = payload[i];
    }
    rx[lengthField] = module_protocol::crc8Smbus(rx, lengthField);
    if (forceBadCrc && rx[1] == module_protocol::kStatusOk &&
        lengthField == module_protocol::kIdentityLengthField)
    {
      rx[lengthField] = static_cast<uint8_t>(rx[lengthField] ^ 0xFF);
    }
  }
};

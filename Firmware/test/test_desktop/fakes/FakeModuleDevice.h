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
  uint8_t sensorState[module_protocol::kMaxSensorsPerModule] = {};
  bool sensorAwaitingSample[module_protocol::kMaxSensorsPerModule] = {};
  uint8_t sensorStateBusyReads[module_protocol::kMaxSensorsPerModule] = {};
  bool shortSolenoidPayload = false;
  uint8_t solenoidCount = 0;
  uint8_t solenoidState[module_protocol::kMaxSolenoidsPerModule] = {};
  bool shortPumpPayload = false;
  uint8_t pumpState = module_protocol::kPumpStateOff;
  bool pumpResetLeavesFault = false;
  uint8_t pumpPhase = module_protocol::kPumpPhaseOff;
  uint8_t pumpFlags = 0;
  uint16_t pumpMillivolts = 0;
  uint16_t pumpMilliamps = 0;
  uint16_t pumpRunSeconds = 0;
  uint16_t pumpCyclePeakMillivolts = 0;
  uint16_t pumpCycleMinimumMillivolts = 0;
  uint16_t pumpCyclePeakMilliamps = 0;
  uint16_t pumpCycleAverageMillivolts = 0;
  uint16_t pumpCycleAverageMilliamps = 0;
  uint16_t pumpCycleSoftStartMs = 0;
  uint16_t pumpCyclePowerGoodMs = 0;
  uint16_t pumpCycleRunSeconds = 0;
  uint32_t pumpCompletedCycles = 0;
  uint32_t pumpTotalSeconds = 0;
  uint16_t pumpLifetimePeakMillivolts = 0;
  uint16_t pumpLifetimePeakMilliamps = 0;
  uint16_t pumpFailedStarts = 0;
  uint16_t pumpLastRunSeconds = 0;
  uint8_t pumpFaultReason = module_protocol::kFaultNone;
  uint8_t pumpFaultIndex = 0;
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
        cmd == module_protocol::kCmdGetSensorReading ||
        cmd == module_protocol::kCmdGetSensorState ||
        cmd == module_protocol::kCmdSetSensor ||
        cmd == module_protocol::kCmdResetSensor)
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
      if (cmd == module_protocol::kCmdGetSensorReading)
      {
        uint8_t body[module_protocol::kSensorReadingPayloadLen];
        body[0] = index;
        body[1] = static_cast<uint8_t>(sensorConnected[index] ? 1 : 0);
        module_protocol::writeInt32Be(body + 2, sensorValue[index]);
        _writeStatus(rx, module_protocol::kStatusOk, body,
                     module_protocol::kSensorReadingPayloadLen);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdSetSensor)
      {
        if (tx[0] < 4)
        {
          _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
          return I2cTxnStatus::Ok;
        }
        const uint8_t desired = tx[3];
        if (desired != module_protocol::kSensorStateOff &&
            desired != module_protocol::kSensorStateOn)
        {
          _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
          return I2cTxnStatus::Ok;
        }
        if (sensorState[index] == module_protocol::kSensorStateFault)
        {
          _replySensorState(rx, index);
          return I2cTxnStatus::Ok;
        }
        if (desired == module_protocol::kSensorStateOff)
        {
          sensorState[index] = module_protocol::kSensorStateOff;
          sensorAwaitingSample[index] = false;
          _replySensorState(rx, index);
          return I2cTxnStatus::Ok;
        }
        if (sensorState[index] == module_protocol::kSensorStateOff &&
            !sensorAwaitingSample[index])
        {
          sensorAwaitingSample[index] = true;
          _writeStatus(rx, module_protocol::kStatusBusy, nullptr, 0);
          return I2cTxnStatus::Ok;
        }
        if (sensorAwaitingSample[index])
        {
          _writeStatus(rx, module_protocol::kStatusBusy, nullptr, 0);
          return I2cTxnStatus::Ok;
        }
        _replySensorState(rx, index);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdResetSensor)
      {
        sensorState[index] = module_protocol::kSensorStateOff;
        sensorAwaitingSample[index] = false;
        sensorStateBusyReads[index] = 0;
        _replySensorState(rx, index);
        return I2cTxnStatus::Ok;
      }
      _replySensorState(rx, index);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdGetSolenoidCount ||
        cmd == module_protocol::kCmdGetSolenoidState ||
        cmd == module_protocol::kCmdSetSolenoid)
    {
      if (shortSolenoidPayload)
      {
        _writeStatus(rx, module_protocol::kStatusOk, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetSolenoidCount)
      {
        _writeStatus(rx, module_protocol::kStatusOk, &solenoidCount, 1);
        return I2cTxnStatus::Ok;
      }
      if (tx[0] < 3)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      const uint8_t index = tx[2];
      if (index >= solenoidCount ||
          index >= module_protocol::kMaxSolenoidsPerModule)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetSolenoidState)
      {
        const uint8_t body[2] = {index, solenoidState[index]};
        _writeStatus(rx, module_protocol::kStatusOk, body, 2);
        return I2cTxnStatus::Ok;
      }
      if (tx[0] < 4)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      const uint8_t desired = tx[3];
      if (desired != module_protocol::kSolenoidStateOff &&
          desired != module_protocol::kSolenoidStateOn)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (solenoidState[index] != module_protocol::kSolenoidStateDisconnected)
      {
        solenoidState[index] = desired;
      }
      const uint8_t body[2] = {index, solenoidState[index]};
      _writeStatus(rx, module_protocol::kStatusOk, body, 2);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdGetPumpState ||
        cmd == module_protocol::kCmdSetPump ||
        cmd == module_protocol::kCmdResetPump)
    {
      if (shortPumpPayload)
      {
        _writeStatus(rx, module_protocol::kStatusOk, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdResetPump)
      {
        if (!(pumpResetLeavesFault &&
              pumpState == module_protocol::kPumpStateFault))
        {
          pumpState = module_protocol::kPumpStateOff;
        }
        _writeStatus(rx, module_protocol::kStatusOk, &pumpState, 1);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetPumpState)
      {
        _writeStatus(rx, module_protocol::kStatusOk, &pumpState, 1);
        return I2cTxnStatus::Ok;
      }
      if (tx[0] < 3)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      const uint8_t desired = tx[2];
      if (desired != module_protocol::kPumpStateOff &&
          desired != module_protocol::kPumpStateOn)
      {
        _writeStatus(rx, module_protocol::kStatusBadLength, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (pumpState != module_protocol::kPumpStateFault)
      {
        pumpState = desired;
      }
      _writeStatus(rx, module_protocol::kStatusOk, &pumpState, 1);
      return I2cTxnStatus::Ok;
    }
    if (cmd == module_protocol::kCmdGetPumpNow ||
        cmd == module_protocol::kCmdGetPumpCycle ||
        cmd == module_protocol::kCmdGetPumpTotals ||
        cmd == module_protocol::kCmdGetFault)
    {
      if (shortPumpPayload)
      {
        _writeStatus(rx, module_protocol::kStatusOk, nullptr, 0);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetFault)
      {
        const uint8_t body[module_protocol::kFaultPayloadLen] = {
          pumpFaultReason, pumpFaultIndex};
        _writeStatus(rx, module_protocol::kStatusOk, body,
                     module_protocol::kFaultPayloadLen);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetPumpNow)
      {
        uint8_t body[module_protocol::kPumpNowPayloadLen];
        body[0] = pumpPhase;
        body[1] = pumpFlags;
        _putU16(body + 2, pumpMillivolts);
        _putU16(body + 4, pumpMilliamps);
        _putU16(body + 6, pumpRunSeconds);
        _writeStatus(rx, module_protocol::kStatusOk, body,
                     module_protocol::kPumpNowPayloadLen);
        return I2cTxnStatus::Ok;
      }
      if (cmd == module_protocol::kCmdGetPumpCycle)
      {
        uint8_t body[module_protocol::kPumpCyclePayloadLen];
        _putU16(body, pumpCyclePeakMillivolts);
        _putU16(body + 2, pumpCycleMinimumMillivolts);
        _putU16(body + 4, pumpCyclePeakMilliamps);
        _putU16(body + 6, pumpCycleAverageMillivolts);
        _putU16(body + 8, pumpCycleAverageMilliamps);
        _putU16(body + 10, pumpCycleSoftStartMs);
        _putU16(body + 12, pumpCyclePowerGoodMs);
        _putU16(body + 14, pumpCycleRunSeconds);
        _writeStatus(rx, module_protocol::kStatusOk, body,
                     module_protocol::kPumpCyclePayloadLen);
        return I2cTxnStatus::Ok;
      }
      uint8_t body[module_protocol::kPumpTotalsPayloadLen];
      _putU32(body, pumpCompletedCycles);
      _putU32(body + 4, pumpTotalSeconds);
      _putU16(body + 8, pumpLifetimePeakMillivolts);
      _putU16(body + 10, pumpLifetimePeakMilliamps);
      _putU16(body + 12, pumpFailedStarts);
      _putU16(body + 14, pumpLastRunSeconds);
      _writeStatus(rx, module_protocol::kStatusOk, body,
                   module_protocol::kPumpTotalsPayloadLen);
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
   * Replies with one input's state. A loop that just closed stays
   * Busy until the held busy reads are spent, then becomes on or
   * disconnected from the connected flag.
   *
   * @param rx Destination frame.
   * @param index Zero-based input.
   * @return Nothing.
   */
  void _replySensorState(uint8_t* rx, uint8_t index)
  {
    if (sensorAwaitingSample[index])
    {
      if (sensorStateBusyReads[index] > 0)
      {
        sensorStateBusyReads[index] =
            static_cast<uint8_t>(sensorStateBusyReads[index] - 1);
        _writeStatus(rx, module_protocol::kStatusBusy, nullptr, 0);
        return;
      }
      sensorAwaitingSample[index] = false;
      sensorState[index] = sensorConnected[index]
                               ? module_protocol::kSensorStateOn
                               : module_protocol::kSensorStateDisconnected;
    }
    const uint8_t body[module_protocol::kSensorStatePayloadLen] = {
      index, sensorState[index]};
    _writeStatus(rx, module_protocol::kStatusOk, body,
                 module_protocol::kSensorStatePayloadLen);
  }

  /**
   * Writes a big-endian uint16.
   *
   * @param dest Two-byte destination.
   * @param value Value to store.
   * @return Nothing.
   */
  void _putU16(uint8_t* dest, uint16_t value)
  {
    dest[0] = static_cast<uint8_t>(value >> 8);
    dest[1] = static_cast<uint8_t>(value);
  }

  /**
   * Writes a big-endian uint32.
   *
   * @param dest Four-byte destination.
   * @param value Value to store.
   * @return Nothing.
   */
  void _putU32(uint8_t* dest, uint32_t value)
  {
    dest[0] = static_cast<uint8_t>(value >> 24);
    dest[1] = static_cast<uint8_t>(value >> 16);
    dest[2] = static_cast<uint8_t>(value >> 8);
    dest[3] = static_cast<uint8_t>(value);
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

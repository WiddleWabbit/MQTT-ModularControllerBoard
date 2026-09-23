#include "ModuleHost.h"

#include "ModuleCodec.h"
#include "ModuleProtocol.h"

// ========== Construction ==========

ModuleHost::ModuleHost(I2cMaster& bus, IClock& clock, SlotPins slots[4],
                       const ModuleHostConfig& config)
  : _bus(bus),
    _clock(clock),
    _config(config),
    _slots{SlotController(slots[0].sense, slots[0].mod, slots[0].cs, clock, 0,
                          config),
           SlotController(slots[1].sense, slots[1].mod, slots[1].cs, clock, 1,
                          config),
           SlotController(slots[2].sense, slots[2].mod, slots[2].cs, clock, 2,
                          config),
           SlotController(slots[3].sense, slots[3].mod, slots[3].cs, clock, 3,
                          config)}
{
}


// ========== Public API ==========

/**
 * Configures SENSE pull-ups, MOD inputs, CS pull-ups, then starts I2C.
 * No-op if already started.
 *
 * @return Nothing.
 */
void ModuleHost::begin()
{
  if (_started)
  {
    return;
  }

  for (uint8_t i = 0; i < module_protocol::kSlotCount; ++i)
  {
    _slots[i].configureIdlePins();
  }
  _bus.begin();
  _bus.setClockHz(_config.i2cClockHz);
  _bus.setTimeoutMs(_config.i2cTimeoutMs);
  _started = true;
}

/**
 * Advances debounce, enumeration, and health. No-op until begin().
 * Issues at most one write/read/writeRead. May call recover() once
 * after Timeout or BusError.
 *
 * @return Nothing.
 */
void ModuleHost::update()
{
  if (!_started)
  {
    return;
  }

  for (uint8_t i = 0; i < module_protocol::kSlotCount; ++i)
  {
    _slots[i].updatePinsAndState();
  }
  _reconcileLock();

  int8_t txnSlot = -1;
  SlotI2cProposal proposal{SlotI2cOp::None, 0};

  if (_lockOwner >= 0)
  {
    proposal = _slots[_lockOwner].proposedI2c();
    if (proposal.op != SlotI2cOp::None && proposal.op != SlotI2cOp::HealthPing)
    {
      txnSlot = _lockOwner;
    }
  }

  if (txnSlot < 0)
  {
    for (uint8_t n = 0; n < module_protocol::kSlotCount; ++n)
    {
      const uint8_t i =
          static_cast<uint8_t>((_nextHealthSlot + n) % module_protocol::kSlotCount);
      const SlotI2cProposal candidate = _slots[i].proposedI2c();
      if (candidate.op == SlotI2cOp::HealthPing)
      {
        txnSlot = static_cast<int8_t>(i);
        proposal = candidate;
        _nextHealthSlot =
            static_cast<uint8_t>((i + 1) % module_protocol::kSlotCount);
        break;
      }
    }
  }

  if (txnSlot < 0)
  {
    return;
  }

  const ModuleStepResult result =
      _issue(static_cast<uint8_t>(txnSlot), proposal);
  _slots[txnSlot].applyI2cResult(result);
  _reconcileLock();
}

/**
 * Reads one slot's public state.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Public slot state, or Empty when the index is invalid.
 */
SlotState ModuleHost::state(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? SlotState::Empty : slot->state();
}

/**
 * Reads one slot's latched assigned address.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return 7-bit address, or 0.
 */
uint8_t ModuleHost::address(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? 0 : slot->address();
}

/**
 * Reads one slot's type id.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Type id, or 0.
 */
uint16_t ModuleHost::typeId(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? 0 : slot->typeId();
}

/**
 * Reads one slot's protocol version.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Protocol version, or 0.
 */
uint8_t ModuleHost::protocolVersion(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? 0 : slot->protocolVersion();
}

/**
 * Reads one slot's firmware version.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Firmware version, or 0.
 */
uint16_t ModuleHost::firmwareVersion(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? 0 : slot->firmwareVersion();
}

/**
 * Reads one slot's last fault tag.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Fault, or None.
 */
SlotFault ModuleHost::fault(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? SlotFault::None : slot->fault();
}

/**
 * Reads the registered type name when the slot is Online.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Type name, or nullptr.
 */
const char* ModuleHost::typeName(uint8_t slotIndex) const
{
  const SlotController* slot = _slot(slotIndex);
  return slot == nullptr ? nullptr : slot->typeName();
}

/**
 * Reads the current enumeration-lock owner.
 *
 * @return Slot index 0..3, or -1 when free.
 */
int8_t ModuleHost::enumLockOwner() const
{
  return _lockOwner;
}

/**
 * Issues one extra PING to an Online or Unsupported slot.
 * Not re-entrant with update(), ping(), or echo().
 *
 * @param slotIndex Firmware slot 0..3.
 * @return True when the ping succeeds.
 */
bool ModuleHost::ping(uint8_t slotIndex)
{
  SlotController* slot = _slot(slotIndex);
  if (slot == nullptr)
  {
    return false;
  }
  const SlotState current = slot->state();
  if ((current != SlotState::Online && current != SlotState::Unsupported) ||
      slot->address() == 0)
  {
    return false;
  }

  uint8_t tx[module_protocol::kMaxFrameBytes];
  uint8_t rx[module_protocol::kMaxFrameBytes];
  const size_t txLen =
      ModuleCodec::encodePing(tx, module_protocol::kMaxFrameBytes);
  const I2cTxnStatus txn =
      _bus.writeRead(slot->address(), tx, txLen, rx,
                     module_protocol::kMaxFrameBytes);
  const ModuleStepResult result = _classifyRead(txn, rx, true, false);
  return result.status == SlotFault::None;
}

/**
 * Issues IdentityEcho ECHO. False if the slot is not Online
 * IdentityEcho or inLen is greater than kMaxPayload.
 *
 * @param slotIndex Firmware slot 0..3.
 * @param in Payload to send.
 * @param inLen Payload length.
 * @param out Destination for the echoed payload.
 * @param outLen Set to the echoed length on success.
 * @return True when echo succeeds.
 */
bool ModuleHost::echo(uint8_t slotIndex, const uint8_t* in, size_t inLen,
                      uint8_t* out, size_t* outLen)
{
  SlotController* slot = _slot(slotIndex);
  if (slot == nullptr)
  {
    return false;
  }
  if (inLen > module_protocol::kMaxPayload)
  {
    return false;
  }
  if (inLen > 0 && in == nullptr)
  {
    return false;
  }
  if (slot->state() != SlotState::Online ||
      slot->typeId() != module_protocol::kTypeIdentityEcho)
  {
    return false;
  }
  if (out == nullptr || outLen == nullptr)
  {
    return false;
  }

  uint8_t tx[module_protocol::kMaxFrameBytes];
  uint8_t rx[module_protocol::kMaxFrameBytes];
  const size_t txLen = ModuleCodec::encodeEcho(
      in, inLen, tx, module_protocol::kMaxFrameBytes);
  const I2cTxnStatus txn =
      _bus.writeRead(slot->address(), tx, txLen, rx,
                     module_protocol::kMaxFrameBytes);
  const ModuleStepResult result = _classifyRead(txn, rx, false, false);
  if (result.status != SlotFault::None)
  {
    return false;
  }
  if (result.payloadLen != inLen)
  {
    return false;
  }
  for (uint8_t i = 0; i < result.payloadLen; ++i)
  {
    out[i] = result.payload[i];
  }
  *outLen = result.payloadLen;
  return true;
}

/**
 * Returns the active host configuration.
 *
 * @return Timing and bus configuration.
 */
const ModuleHostConfig& ModuleHost::config() const
{
  return _config;
}


// ========== Private Helpers ==========

/**
 * Returns a slot pointer when the index is 0..3.
 *
 * @param slotIndex Firmware slot index.
 * @return Slot pointer, or nullptr.
 */
SlotController* ModuleHost::_slot(uint8_t slotIndex)
{
  if (slotIndex >= module_protocol::kSlotCount)
  {
    return nullptr;
  }
  return &_slots[slotIndex];
}

/**
 * Returns a const slot pointer when the index is 0..3.
 *
 * @param slotIndex Firmware slot index.
 * @return Slot pointer, or nullptr.
 */
const SlotController* ModuleHost::_slot(uint8_t slotIndex) const
{
  if (slotIndex >= module_protocol::kSlotCount)
  {
    return nullptr;
  }
  return &_slots[slotIndex];
}

/**
 * Grants or releases the enumeration lock to match slot wants.
 *
 * @return Nothing.
 */
void ModuleHost::_reconcileLock()
{
  if (_lockOwner >= 0 &&
      !_slots[_lockOwner].wantsEnumLock())
  {
    _slots[_lockOwner].notifyEnumLock(false);
    _lockOwner = -1;
  }

  if (_lockOwner < 0)
  {
    for (uint8_t i = 0; i < module_protocol::kSlotCount; ++i)
    {
      if (_slots[i].wantsEnumLock())
      {
        _lockOwner = static_cast<int8_t>(i);
        _slots[i].notifyEnumLock(true);
        break;
      }
    }
  }
}

/**
 * Issues the proposed transaction and classifies the result.
 *
 * @param slotIndex Slot that proposed the operation.
 * @param proposal Proposed transaction.
 * @return Classified step result.
 */
ModuleStepResult ModuleHost::_issue(uint8_t slotIndex,
                                    const SlotI2cProposal& proposal)
{
  uint8_t tx[module_protocol::kMaxFrameBytes];
  uint8_t rx[module_protocol::kMaxFrameBytes];
  size_t txLen = 0;

  if (proposal.op == SlotI2cOp::SetAddress)
  {
    txLen = ModuleCodec::encodeSetAddress(_slots[slotIndex].targetAddress(),
                                          tx, module_protocol::kMaxFrameBytes);
    const I2cTxnStatus txn = _bus.write(proposal.i2cAddress, tx, txLen);
    if (txn != I2cTxnStatus::Ok)
    {
      return _classifyBusError(txn);
    }
    ModuleStepResult ok{};
    ok.status = SlotFault::None;
    return ok;
  }

  if (proposal.op == SlotI2cOp::GetIdentity)
  {
    txLen = ModuleCodec::encodeGetIdentity(tx, module_protocol::kMaxFrameBytes);
    const I2cTxnStatus txn =
        _bus.writeRead(proposal.i2cAddress, tx, txLen, rx,
                       module_protocol::kMaxFrameBytes);
    return _classifyRead(txn, rx, false, true);
  }

  txLen = ModuleCodec::encodePing(tx, module_protocol::kMaxFrameBytes);
  const I2cTxnStatus txn =
      _bus.writeRead(proposal.i2cAddress, tx, txLen, rx,
                     module_protocol::kMaxFrameBytes);
  return _classifyRead(txn, rx, true, false);
}

/**
 * Classifies a completed writeRead of a response frame.
 *
 * @param txn Bus transaction status.
 * @param rx 19-byte read buffer, valid only when txn is Ok.
 * @param ping True when the command was PING (Ok requires len == 2).
 * @param identity True when the command was GET_IDENTITY (Ok requires
 *        len == 7).
 * @return Classified step result.
 */
ModuleStepResult ModuleHost::_classifyRead(I2cTxnStatus txn, const uint8_t* rx,
                                           bool ping, bool identity)
{
  if (txn != I2cTxnStatus::Ok)
  {
    return _classifyBusError(txn);
  }

  const ModuleDecodedFrame decoded =
      ModuleCodec::decode(rx, module_protocol::kMaxFrameBytes);
  ModuleStepResult result{};
  result.cmdOrStatus = decoded.statusByte;
  result.payloadLen = decoded.payloadLen;
  for (uint8_t i = 0; i < decoded.payloadLen; ++i)
  {
    result.payload[i] = decoded.payload[i];
  }

  if (decoded.decodeStatus == ModuleDecodeStatus::BadCrc)
  {
    result.status = SlotFault::BadCrc;
    return result;
  }
  if (decoded.decodeStatus != ModuleDecodeStatus::Ok)
  {
    result.status = SlotFault::BadFrame;
    return result;
  }
  if (decoded.statusByte == module_protocol::kStatusBusy)
  {
    result.status = SlotFault::Busy;
    return result;
  }
  if (decoded.statusByte != module_protocol::kStatusOk)
  {
    result.status = SlotFault::BadFrame;
    return result;
  }
  if (ping && decoded.payloadLen != 0)
  {
    result.status = SlotFault::BadFrame;
    return result;
  }
  if (identity && decoded.payloadLen != module_protocol::kIdentityPayloadLen)
  {
    result.status = SlotFault::BadFrame;
    return result;
  }

  result.status = SlotFault::None;
  return result;
}

/**
 * Maps a bus error to a step result and recovers on timeout.
 *
 * @param txn Bus transaction status.
 * @return Classified step result for a non-Ok transaction.
 */
ModuleStepResult ModuleHost::_classifyBusError(I2cTxnStatus txn)
{
  ModuleStepResult result{};
  if (txn == I2cTxnStatus::Nack)
  {
    result.status = SlotFault::Nack;
    return result;
  }
  result.status = SlotFault::Timeout;
  _bus.recover();
  return result;
}

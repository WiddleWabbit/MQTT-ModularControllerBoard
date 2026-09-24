#include "SlotController.h"

namespace
{
struct ModuleTypeInfo
{
  uint16_t typeId;
  const char* name;
};

const ModuleTypeInfo kModuleTypes[] = {
  {module_protocol::kTypeIdentityEcho, "IdentityEcho"},
  {module_protocol::kTypeSensorModule, "Sensor"},
};

/**
 * Looks up a registered type name.
 *
 * @param typeId Module type id.
 * @param protocolVersion Protocol version from identity.
 * @return Name when type is known and protocol is v1, otherwise nullptr.
 */
const char* lookupTypeName(uint16_t typeId, uint8_t protocolVersion)
{
  if (protocolVersion != module_protocol::kProtocolVersion)
  {
    return nullptr;
  }
  for (const ModuleTypeInfo& info : kModuleTypes)
  {
    if (info.typeId == typeId)
    {
      return info.name;
    }
  }
  return nullptr;
}
}


// ========== Construction ==========

SlotController::SlotController(IDigitalPin& sense, IDigitalPin& mod,
                               IDigitalPin& cs, IClock& clock,
                               uint8_t slotIndex,
                               const ModuleHostConfig& config)
  : _sense(sense),
    _mod(mod),
    _cs(cs),
    _clock(clock),
    _slotIndex(slotIndex),
    _config(config)
{
}


// ========== Public API ==========

/**
 * Sets SENSE to input-pullup, MOD to input, and CS to input-pullup.
 *
 * @return Nothing.
 */
void SlotController::configureIdlePins()
{
  _sense.setMode(PinMode::DigitalInputPullup);
  _mod.setMode(PinMode::DigitalInput);
  _cs.setMode(PinMode::DigitalInputPullup);
}

/**
 * Advances GPIO, debounce, timers, and MOD. No I2C.
 *
 * @return Nothing.
 */
void SlotController::updatePinsAndState()
{
  const uint32_t now = _clock.millis();
  const bool present = _isPresent();

  if (_phase == Phase::Empty)
  {
    if (present)
    {
      _phase = Phase::DebouncePresent;
      _phaseStartedAt = now;
    }
    return;
  }

  if (_phase == Phase::DebouncePresent)
  {
    if (!present)
    {
      _enterEmpty();
      return;
    }
    if (_hasElapsed(now, _phaseStartedAt, _config.presenceDebounceMs))
    {
      _phase = Phase::BootWait;
      _phaseStartedAt = now;
    }
    return;
  }

  if (_phase == Phase::DebounceAbsent)
  {
    if (present)
    {
      _phase = _phaseBeforeAbsent;
      return;
    }
    if (_hasElapsed(now, _phaseStartedAt, _config.presenceDebounceMs))
    {
      _enterEmpty();
    }
    return;
  }

  if (!present)
  {
    _phaseBeforeAbsent = _phase;
    _phase = Phase::DebounceAbsent;
    _phaseStartedAt = now;
    return;
  }

  if (_phase == Phase::BootWait)
  {
    if (_hasElapsed(now, _phaseStartedAt, _config.bootWaitMs))
    {
      _phase = Phase::WaitForLock;
    }
    return;
  }

  if (_phase == Phase::SelectAssert)
  {
    if (_hasElapsed(now, _phaseStartedAt, _config.modSettleMs))
    {
      _enterStep(Phase::ProbeDefault);
    }
    return;
  }

  if (_phase == Phase::RecoverSelect)
  {
    if (_hasElapsed(now, _phaseStartedAt, _config.modSettleMs))
    {
      _enterStep(Phase::RecoverProbeDefault);
    }
    return;
  }

  if (_phase == Phase::Fault)
  {
    if (_hasElapsed(now, _phaseStartedAt, _config.faultRetryMs))
    {
      if (_committedAddress != 0)
      {
        _phase = Phase::RecoverWaitLock;
      }
      else
      {
        _phase = Phase::WaitForLock;
      }
    }
  }
}

/**
 * Reports whether this slot needs the host enumeration lock.
 *
 * @return True when the slot is waiting for or holding the lock.
 */
bool SlotController::wantsEnumLock() const
{
  if (_phase == Phase::DebounceAbsent)
  {
    return _holdsLock(_phaseBeforeAbsent) || _lockHeld;
  }
  return _holdsLock(_phase);
}

/**
 * Tells the slot whether it currently holds the enumeration lock.
 *
 * @param held True when this slot owns the lock.
 * @return Nothing.
 */
void SlotController::notifyEnumLock(bool held)
{
  _lockHeld = held;
  if (!held)
  {
    return;
  }
  if (_phase == Phase::WaitForLock)
  {
    _assertSelect();
    _phase = Phase::SelectAssert;
    _phaseStartedAt = _clock.millis();
  }
  else if (_phase == Phase::RecoverWaitLock)
  {
    _assertSelect();
    _phase = Phase::RecoverSelect;
    _phaseStartedAt = _clock.millis();
  }
}

/**
 * Returns the I2C operation due this tick, or None.
 *
 * @return Proposed transaction.
 */
SlotI2cProposal SlotController::proposedI2c() const
{
  SlotI2cProposal proposal{SlotI2cOp::None, 0};
  const uint32_t now = _clock.millis();

  if (_phase == Phase::Online || _phase == Phase::Unsupported)
  {
    if (_address != 0 && _hasElapsed(now, _lastHealthAt, _config.healthPingMs))
    {
      proposal.op = SlotI2cOp::HealthPing;
      proposal.i2cAddress = _address;
    }
    return proposal;
  }

  if (!_lockHeld || !_stepDue(now))
  {
    return proposal;
  }

  switch (_phase)
  {
    case Phase::ProbeDefault:
    case Phase::RecoverProbeDefault:
      proposal.op = SlotI2cOp::PingUnconfigured;
      proposal.i2cAddress = module_protocol::kUnconfiguredAddress;
      break;
    case Phase::SetAddress:
      proposal.op = SlotI2cOp::SetAddress;
      proposal.i2cAddress = module_protocol::kUnconfiguredAddress;
      break;
    case Phase::VerifyAssigned:
    case Phase::RecoverProbeAssigned:
      proposal.op = SlotI2cOp::PingAssigned;
      proposal.i2cAddress = _committedAddress;
      break;
    case Phase::Identify:
      proposal.op = SlotI2cOp::GetIdentity;
      proposal.i2cAddress = _address;
      break;
    default:
      break;
  }
  return proposal;
}

/**
 * Applies the classified result of the host's one transaction.
 *
 * @param result Classified protocol result.
 * @return Nothing.
 */
void SlotController::applyI2cResult(const ModuleStepResult& result)
{
  const uint32_t now = _clock.millis();

  if (_phase == Phase::Online || _phase == Phase::Unsupported)
  {
    _lastHealthAt = now;
    if (result.status == SlotFault::Busy)
    {
      return;
    }
    if (result.status == SlotFault::None)
    {
      _healthFails = 0;
      return;
    }
    _healthFails = static_cast<uint8_t>(_healthFails + 1);
    _fault = result.status;
    if (_healthFails >= _config.healthFailLimit)
    {
      _healthFails = 0;
      _priorPublic = state();
      _phase = Phase::RecoverWaitLock;
    }
    return;
  }

  if (_phase == Phase::RecoverProbeDefault)
  {
    if (result.status == SlotFault::None)
    {
      _forgetIdentity();
      _enterStep(Phase::SetAddress);
      return;
    }
    if (result.status == SlotFault::Nack)
    {
      _enterStep(Phase::RecoverProbeAssigned);
      return;
    }
    _noteAttemptFail(result.status);
    return;
  }

  if (_phase == Phase::RecoverProbeAssigned)
  {
    if (result.status == SlotFault::None)
    {
      if (_typeId == 0)
      {
        if (_address == 0)
        {
          _address = _committedAddress;
        }
        _releaseMod();
        _enterStep(Phase::Identify);
        return;
      }
      _releaseMod();
      _lastHealthAt = now;
      _healthFails = 0;
      if (_priorPublic == SlotState::Unsupported)
      {
        _phase = Phase::Unsupported;
      }
      else
      {
        _phase = Phase::Online;
      }
      return;
    }
    _noteAttemptFail(result.status);
    return;
  }

  if (_phase == Phase::ProbeDefault)
  {
    if (result.status == SlotFault::None)
    {
      _enterStep(Phase::SetAddress);
      return;
    }
    _noteAttemptFail(result.status);
    return;
  }

  if (_phase == Phase::SetAddress)
  {
    if (result.status == SlotFault::None)
    {
      _committedAddress = targetAddress();
      _enterStep(Phase::VerifyAssigned);
      return;
    }
    _noteAttemptFail(result.status);
    return;
  }

  if (_phase == Phase::VerifyAssigned)
  {
    if (result.status == SlotFault::None)
    {
      _address = _committedAddress;
      _releaseMod();
      _enterStep(Phase::Identify);
      return;
    }
    _noteAttemptFail(result.status);
    return;
  }

  if (_phase == Phase::Identify)
  {
    if (result.status == SlotFault::None)
    {
      _applyIdentity(result);
      return;
    }
    _noteAttemptFail(result.status);
  }
}

/**
 * Reads the public snapshot state.
 *
 * @return Public slot state.
 */
SlotState SlotController::state() const
{
  if (_phase == Phase::DebounceAbsent)
  {
    return _publicOf(_phaseBeforeAbsent);
  }
  return _publicOf(_phase);
}

/**
 * Reads the latched assigned address.
 *
 * @return 7-bit address, or 0 before VerifyAssigned success.
 */
uint8_t SlotController::address() const
{
  return _address;
}

/**
 * Reads the last committed SET_ADDRESS target.
 *
 * @return 7-bit address, or 0 if SET_ADDRESS never succeeded.
 */
uint8_t SlotController::committedAddress() const
{
  return _committedAddress;
}

/**
 * Reads the identified type id.
 *
 * @return Type id, or 0 if unknown.
 */
uint16_t SlotController::typeId() const
{
  return _typeId;
}

/**
 * Reads the identified protocol version.
 *
 * @return Protocol version, or 0 if unknown.
 */
uint8_t SlotController::protocolVersion() const
{
  return _protocolVersion;
}

/**
 * Reads the identified firmware version.
 *
 * @return Firmware version, or 0 if unknown.
 */
uint16_t SlotController::firmwareVersion() const
{
  return _firmwareVersion;
}

/**
 * Reads the identity epoch. Increments each time GET_IDENTITY
 * succeeds, including after a module restart.
 *
 * @return Epoch, or 0 before the first successful identify.
 */
uint32_t SlotController::identityEpoch() const
{
  return _identityEpoch;
}

/**
 * Reads the last fault tag.
 *
 * @return Fault, or None.
 */
SlotFault SlotController::fault() const
{
  return _fault;
}

/**
 * Reads the registered type name when Online.
 *
 * @return Type name, or nullptr.
 */
const char* SlotController::typeName() const
{
  if (state() != SlotState::Online)
  {
    return nullptr;
  }
  return lookupTypeName(_typeId, _protocolVersion);
}

/**
 * Returns the slot-derived address this slot will assign.
 *
 * @return Address 0x10 plus slot index.
 */
uint8_t SlotController::targetAddress() const
{
  return module_protocol::slotAddress(_slotIndex);
}


// ========== Private Helpers ==========

/**
 * Tests elapsed time using wrap-safe unsigned arithmetic.
 *
 * @param now Current monotonic time.
 * @param since Start time.
 * @param duration Required duration.
 * @return True when duration has elapsed.
 */
bool SlotController::_hasElapsed(uint32_t now, uint32_t since,
                                 uint32_t duration)
{
  return static_cast<uint32_t>(now - since) >= duration;
}

/**
 * Maps an internal phase to the public snapshot.
 *
 * @param phase Internal phase.
 * @return Public slot state.
 */
SlotState SlotController::_publicOf(Phase phase)
{
  switch (phase)
  {
    case Phase::Empty:
      return SlotState::Empty;
    case Phase::DebouncePresent:
      return SlotState::Debouncing;
    case Phase::Online:
      return SlotState::Online;
    case Phase::Unsupported:
      return SlotState::Unsupported;
    case Phase::Fault:
      return SlotState::Fault;
    default:
      return SlotState::Enumerating;
  }
}

/**
 * Reports whether a phase is part of lock-held enumeration.
 *
 * @param phase Internal phase.
 * @return True when the phase must hold the enum lock.
 */
bool SlotController::_holdsLock(Phase phase)
{
  switch (phase)
  {
    case Phase::WaitForLock:
    case Phase::SelectAssert:
    case Phase::ProbeDefault:
    case Phase::SetAddress:
    case Phase::VerifyAssigned:
    case Phase::Identify:
    case Phase::RecoverWaitLock:
    case Phase::RecoverSelect:
    case Phase::RecoverProbeDefault:
    case Phase::RecoverProbeAssigned:
      return true;
    default:
      return false;
  }
}

/**
 * Reports whether sense currently indicates a seated module.
 *
 * @return True when the sense pin is LOW.
 */
bool SlotController::_isPresent() const
{
  return !_sense.read();
}

/**
 * Reports whether the current enumeration/recover step may issue I2C.
 *
 * @param now Current monotonic time.
 * @return True when an attempt is due.
 */
bool SlotController::_stepDue(uint32_t now) const
{
  switch (_phase)
  {
    case Phase::ProbeDefault:
    case Phase::SetAddress:
    case Phase::VerifyAssigned:
    case Phase::Identify:
    case Phase::RecoverProbeDefault:
    case Phase::RecoverProbeAssigned:
      break;
    default:
      return false;
  }
  if (_waitingRetry)
  {
    return _hasElapsed(now, _retryAt, _config.retryGapMs);
  }
  return true;
}

/**
 * Enters Empty, releases MOD, and forgets identity and addresses.
 *
 * @return Nothing.
 */
void SlotController::_enterEmpty()
{
  _releaseMod();
  _phase = Phase::Empty;
  _phaseBeforeAbsent = Phase::Empty;
  _priorPublic = SlotState::Empty;
  _lockHeld = false;
  _waitingRetry = false;
  _attempts = 0;
  _healthFails = 0;
  _address = 0;
  _committedAddress = 0;
  _typeId = 0;
  _protocolVersion = 0;
  _firmwareVersion = 0;
  _fault = SlotFault::None;
}

/**
 * Asserts MOD open-drain LOW and starts the settle timer.
 *
 * @return Nothing.
 */
void SlotController::_assertSelect()
{
  _mod.setMode(PinMode::DigitalOutputOpenDrain);
  _mod.write(false);
}

/**
 * Releases MOD to DigitalInput.
 *
 * @return Nothing.
 */
void SlotController::_releaseMod()
{
  _mod.setMode(PinMode::DigitalInput);
}

/**
 * Starts a new I2C step with attempt count zero.
 *
 * @param phase New internal phase.
 * @return Nothing.
 */
void SlotController::_enterStep(Phase phase)
{
  _phase = phase;
  _attempts = 0;
  _waitingRetry = false;
  _phaseStartedAt = _clock.millis();
}

/**
 * Records a failed attempt and faults after commandRetries.
 *
 * @param status Classified failure.
 * @return Nothing.
 */
void SlotController::_noteAttemptFail(SlotFault status)
{
  _fault = status;
  _attempts = static_cast<uint8_t>(_attempts + 1);
  if (_attempts >= _config.commandRetries)
  {
    _enterFault(status);
    return;
  }
  _waitingRetry = true;
  _retryAt = _clock.millis();
}

/**
 * Enters Fault, records the tag, and stops wanting the lock.
 *
 * @param status Fault tag.
 * @return Nothing.
 */
void SlotController::_enterFault(SlotFault status)
{
  _fault = status;
  _phase = Phase::Fault;
  _phaseStartedAt = _clock.millis();
  _waitingRetry = false;
  _attempts = 0;
  _releaseMod();
}

/**
 * Parses a successful GET_IDENTITY payload into Online or Unsupported.
 *
 * @param result Identity step result.
 * @return Nothing.
 */
void SlotController::_applyIdentity(const ModuleStepResult& result)
{
  if (result.payloadLen != module_protocol::kIdentityPayloadLen)
  {
    _noteAttemptFail(SlotFault::BadFrame);
    return;
  }

  module_protocol::IdentityPayload identity{};
  identity.typeIdHi = result.payload[0];
  identity.typeIdLo = result.payload[1];
  identity.protocolVersion = result.payload[2];
  identity.firmwareVersionHi = result.payload[3];
  identity.firmwareVersionLo = result.payload[4];
  _typeId = module_protocol::identityTypeId(identity);
  _protocolVersion = identity.protocolVersion;
  _firmwareVersion = module_protocol::identityFirmwareVersion(identity);
  _identityEpoch += 1;
  _lastHealthAt = _clock.millis();
  _healthFails = 0;
  _fault = SlotFault::None;
  if (lookupTypeName(_typeId, _protocolVersion) != nullptr)
  {
    _phase = Phase::Online;
    _priorPublic = SlotState::Online;
  }
  else
  {
    _phase = Phase::Unsupported;
    _priorPublic = SlotState::Unsupported;
  }
}

/**
 * Clears identity RAM while keeping committedAddress.
 *
 * @return Nothing.
 */
void SlotController::_forgetIdentity()
{
  _typeId = 0;
  _protocolVersion = 0;
  _firmwareVersion = 0;
  _address = 0;
  _priorPublic = SlotState::Empty;
}

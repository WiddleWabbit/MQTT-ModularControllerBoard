#include "SolenoidPoller.h"

// ========== Construction ==========

SolenoidPoller::SolenoidPoller(ModuleHost& moduleHost, IClock& clock,
                               uint32_t pollIntervalMs,
                               uint32_t commandTimeoutMs)
  : _moduleHost(moduleHost),
    _clock(clock),
    _intervalMs(pollIntervalMs == 0 ? kDefaultPollIntervalMs : pollIntervalMs),
    _commandTimeoutMs(commandTimeoutMs == 0 ? kDefaultCommandTimeoutMs
                                            : commandTimeoutMs),
    _silenceStarted(false),
    _silenceAnchor(0),
    _failsafeActive(false)
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _clearCache(slot);
    _clearIntent(slot);
  }
}


// ========== Public API ==========

/**
 * Issues at most one solenoid query. No-op when nothing is due.
 * Call after ModuleHost::update() returns.
 *
 * @return Nothing.
 */
void SolenoidPoller::update()
{
  _syncSessions();
  _armSilence();
  if (_serviceCount())
  {
    return;
  }
  if (_serviceApply())
  {
    return;
  }
  _servicePeriodic();
}

/**
 * Records desired on/off states for one module. Does not touch I2C.
 * Restarts the command-absence timer. A later command for the same
 * slot replaces this one. The poller applies it only when that
 * module's reported count matches count.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param desiredOn True commands on. One entry per output.
 * @param count Number of entries, 1..kMaxSolenoidsPerModule.
 * @return False when the slot, pointer, or count is out of range.
 */
bool SolenoidPoller::commandStates(uint8_t moduleSlot, const bool* desiredOn,
                                   uint8_t count)
{
  if (moduleSlot >= module_protocol::kSlotCount || desiredOn == nullptr ||
      count < 1 || count > module_protocol::kMaxSolenoidsPerModule)
  {
    return false;
  }
  _silenceStarted = true;
  _silenceAnchor = _clock.millis();
  _failsafeActive = false;
  Intent& intent = _intent[moduleSlot];
  intent.kind = IntentKind::States;
  intent.tokenCount = count;
  for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
       ++index)
  {
    intent.desiredOn[index] = index < count ? desiredOn[index] : false;
  }
  intent.pending = true;
  intent.applying = false;
  intent.index = 0;
  intent.attempts = 0;
  return true;
}

/**
 * Reports whether the output count for a slot is known.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True after a successful count query for the current identity.
 */
bool SolenoidPoller::countKnown(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return false;
  }
  return _runtime[moduleSlot].countKnown;
}

/**
 * Reads the cached output count.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Count, or 0 when it is not known.
 */
uint8_t SolenoidPoller::solenoidCount(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].countKnown)
  {
    return 0;
  }
  return _runtime[moduleSlot].count;
}

/**
 * Reads the cached state of one output.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @param state Set when a state has been stored.
 * @return False when the count is unknown, the index is outside
 *         that count, or no state has been stored.
 */
bool SolenoidPoller::solenoidState(uint8_t moduleSlot, uint8_t solenoidIndex,
                                   SolenoidOutputState* state) const
{
  if (state == nullptr || moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].countKnown ||
      solenoidIndex >= _runtime[moduleSlot].count)
  {
    return false;
  }
  const Output& output = _runtime[moduleSlot].outputs[solenoidIndex];
  if (!output.known)
  {
    return false;
  }
  *state = output.state;
  return true;
}

/**
 * Reads how many states have been stored for one output.
 * The value increases each time a state is stored, including when
 * the state is unchanged. Zero means no state is stored.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @return State generation, or 0 when none has been stored.
 */
uint32_t SolenoidPoller::stateRevision(uint8_t moduleSlot,
                                       uint8_t solenoidIndex) const
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      solenoidIndex >= module_protocol::kMaxSolenoidsPerModule)
  {
    return 0;
  }
  return _runtime[moduleSlot].outputs[solenoidIndex].revision;
}

/**
 * Reads the poll interval in use.
 *
 * @return Poll interval in milliseconds.
 */
uint32_t SolenoidPoller::pollIntervalMs() const
{
  return _intervalMs;
}

/**
 * Reads the command-absence timeout in use.
 *
 * @return Timeout in milliseconds.
 */
uint32_t SolenoidPoller::commandTimeoutMs() const
{
  return _commandTimeoutMs;
}


// ========== Session Sync ==========

/**
 * Forgets cached outputs when a slot is not an Online Solenoid,
 * and when its identity epoch changes. Drops a desired-state
 * command when the slot can never be a Solenoid module.
 *
 * @return Nothing.
 */
void SolenoidPoller::_syncSessions()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (!_isActiveSolenoid(slot))
    {
      _clearCache(slot);
      if (_intentTargetIsTerminal(slot))
      {
        _clearIntent(slot);
      }
      continue;
    }
    const uint32_t epoch = _moduleHost.identityEpoch(slot);
    if (_runtime[slot].epoch != epoch)
    {
      const Intent saved = _intent[slot];
      _clearCache(slot);
      _runtime[slot].epoch = epoch;
      _intent[slot] = saved;
      if (saved.kind != IntentKind::None)
      {
        _intent[slot].pending = true;
        _intent[slot].applying = false;
        _intent[slot].index = 0;
        _intent[slot].attempts = 0;
      }
    }
  }
}

/**
 * Clears one slot's cached count, cycle, and outputs.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SolenoidPoller::_clearCache(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.epoch = 0;
  runtime.countKnown = false;
  runtime.count = 0;
  runtime.countAttempts = 0;
  runtime.countRetryAt = 0;
  runtime.inCycle = false;
  runtime.index = 0;
  runtime.stepAttempts = 0;
  runtime.nextPollAt = 0;
  for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
       ++index)
  {
    runtime.outputs[index].known = false;
    runtime.outputs[index].state = SolenoidOutputState::Off;
    runtime.outputs[index].revision = 0;
  }
}

/**
 * Forgets a desired-state command for one slot.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SolenoidPoller::_clearIntent(uint8_t moduleSlot)
{
  Intent& intent = _intent[moduleSlot];
  intent.kind = IntentKind::None;
  intent.tokenCount = 0;
  intent.pending = false;
  intent.applying = false;
  intent.index = 0;
  intent.attempts = 0;
  for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
       ++index)
  {
    intent.desiredOn[index] = false;
  }
}

/**
 * Starts the silence window on the first pass, and commands every
 * output off once the window elapses.
 *
 * @return Nothing.
 */
void SolenoidPoller::_armSilence()
{
  if (!_silenceStarted)
  {
    _silenceStarted = true;
    _silenceAnchor = _clock.millis();
  }
  if (!_failsafeActive)
  {
    const uint32_t elapsed =
        static_cast<uint32_t>(_clock.millis() - _silenceAnchor);
    if (elapsed < _commandTimeoutMs)
    {
      return;
    }
    _failsafeActive = true;
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (_intent[slot].kind != IntentKind::AllOff)
    {
      _armAllOff(slot);
    }
  }
}

/**
 * Marks one slot to be driven off. Does not restart the silence window.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SolenoidPoller::_armAllOff(uint8_t moduleSlot)
{
  Intent& intent = _intent[moduleSlot];
  intent.kind = IntentKind::AllOff;
  intent.tokenCount = 0;
  intent.pending = true;
  intent.applying = false;
  intent.index = 0;
  intent.attempts = 0;
  for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
       ++index)
  {
    intent.desiredOn[index] = false;
  }
}

/**
 * Reports whether a slot is an Online Solenoid module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True when solenoid queries are allowed.
 */
bool SolenoidPoller::_isActiveSolenoid(uint8_t moduleSlot) const
{
  return _moduleHost.state(moduleSlot) == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) == module_protocol::kTypeSolenoidModule;
}

/**
 * Reports whether a slot can never host a Solenoid module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True for Empty, Fault, Unsupported, or a different type.
 */
bool SolenoidPoller::_intentTargetIsTerminal(uint8_t moduleSlot) const
{
  const SlotState state = _moduleHost.state(moduleSlot);
  if (state == SlotState::Empty || state == SlotState::Fault ||
      state == SlotState::Unsupported)
  {
    return true;
  }
  return state == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) !=
             module_protocol::kTypeSolenoidModule;
}


// ========== Commands ==========

/**
 * Queries the output count for one module that still needs it.
 *
 * @return True when a count query was issued.
 */
bool SolenoidPoller::_serviceCount()
{
  const int8_t slot = _slotNeedingCount();
  if (slot < 0)
  {
    return false;
  }
  SlotRuntime& runtime = _runtime[slot];
  const SolenoidCountResult count = _moduleHost.querySolenoidCount(
      static_cast<uint8_t>(slot));
  if (count.status == SolenoidQueryStatus::Ok)
  {
    runtime.countKnown = true;
    runtime.count = count.count;
    runtime.countAttempts = 0;
    runtime.countRetryAt = 0;
    runtime.inCycle = false;
    runtime.index = 0;
    runtime.stepAttempts = 0;
    runtime.nextPollAt = 0;
    return true;
  }

  runtime.countAttempts = static_cast<uint8_t>(runtime.countAttempts + 1);
  if (runtime.countAttempts >= kSolenoidQueryAttempts)
  {
    runtime.countAttempts = 0;
    runtime.countRetryAt = _clock.millis() + _intervalMs;
  }
  return true;
}

/**
 * Drives one output toward the desired state, or reads an unknown
 * state needed to decide. Skips outputs that already match.
 *
 * @return True when a query was issued.
 */
bool SolenoidPoller::_serviceApply()
{
  while (true)
  {
    const int8_t selected = _applySlot();
    if (selected < 0)
    {
      return false;
    }
    const uint8_t slot = static_cast<uint8_t>(selected);
    SlotRuntime& runtime = _runtime[slot];
    Intent& intent = _intent[slot];
    if (intent.kind == IntentKind::States &&
        intent.tokenCount != runtime.count)
    {
      _clearIntent(slot);
      continue;
    }
    if (runtime.count == 0)
    {
      intent.pending = false;
      intent.applying = false;
      continue;
    }
    if (!intent.applying)
    {
      intent.applying = true;
      intent.index = 0;
      intent.attempts = 0;
    }
    if (intent.index >= runtime.count)
    {
      intent.pending = false;
      intent.applying = false;
      intent.index = 0;
      intent.attempts = 0;
      continue;
    }

    Output& output = runtime.outputs[intent.index];
    if (!output.known)
    {
      const SolenoidStateResult reading = _moduleHost.querySolenoidState(
          slot, intent.index);
      if (reading.status == SolenoidQueryStatus::Ok)
      {
        _rememberState(slot, intent.index, reading.state);
        intent.attempts = 0;
        return true;
      }
      intent.attempts = static_cast<uint8_t>(intent.attempts + 1);
      if (intent.attempts >= kSolenoidQueryAttempts)
      {
        intent.index = static_cast<uint8_t>(intent.index + 1);
        intent.attempts = 0;
      }
      return true;
    }

    if (_matches(slot, intent.index))
    {
      intent.index = static_cast<uint8_t>(intent.index + 1);
      intent.attempts = 0;
      continue;
    }

    const SolenoidStateResult written = _moduleHost.setSolenoid(
        slot, intent.index, _desiredOn(slot, intent.index));
    if (written.status == SolenoidQueryStatus::Ok)
    {
      _rememberState(slot, intent.index, written.state);
      if (_matches(slot, intent.index))
      {
        intent.index = static_cast<uint8_t>(intent.index + 1);
        intent.attempts = 0;
      }
      else
      {
        intent.attempts = static_cast<uint8_t>(intent.attempts + 1);
        if (intent.attempts >= kSolenoidQueryAttempts)
        {
          intent.index = static_cast<uint8_t>(intent.index + 1);
          intent.attempts = 0;
        }
      }
      return true;
    }
    intent.attempts = static_cast<uint8_t>(intent.attempts + 1);
    if (intent.attempts >= kSolenoidQueryAttempts)
    {
      intent.index = static_cast<uint8_t>(intent.index + 1);
      intent.attempts = 0;
    }
    return true;
  }
}

/**
 * Finds a slot whose count query is due, preferring a commanded slot.
 *
 * @return Slot index, or -1.
 */
int8_t SolenoidPoller::_slotNeedingCount() const
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const Intent& intent = _intent[slot];
    const SlotRuntime& runtime = _runtime[slot];
    if (intent.kind != IntentKind::None &&
        (intent.pending || intent.applying) && _isActiveSolenoid(slot) &&
        !runtime.countKnown && _isDue(runtime.countRetryAt))
    {
      return static_cast<int8_t>(slot);
    }
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const SlotRuntime& runtime = _runtime[slot];
    if (_isActiveSolenoid(slot) && !runtime.countKnown &&
        _isDue(runtime.countRetryAt))
    {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

/**
 * Finds a slot with a desired state still to apply.
 *
 * @return Slot index, or -1.
 */
int8_t SolenoidPoller::_applySlot() const
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const Intent& intent = _intent[slot];
    if (intent.kind == IntentKind::None ||
        (!intent.pending && !intent.applying) ||
        !_isActiveSolenoid(slot) || !_runtime[slot].countKnown)
    {
      continue;
    }
    return static_cast<int8_t>(slot);
  }
  return -1;
}

/**
 * Reports the desired energised state of one output.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @return True when the output should be on.
 */
bool SolenoidPoller::_desiredOn(uint8_t moduleSlot,
                                uint8_t solenoidIndex) const
{
  if (_intent[moduleSlot].kind == IntentKind::AllOff)
  {
    return false;
  }
  return _intent[moduleSlot].desiredOn[solenoidIndex];
}

/**
 * Reports whether a known state already satisfies the desired state.
 * Disconnected outputs match, because the host cannot energise them.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @return True when no SET is required.
 */
bool SolenoidPoller::_matches(uint8_t moduleSlot, uint8_t solenoidIndex) const
{
  const Output& output = _runtime[moduleSlot].outputs[solenoidIndex];
  if (!output.known)
  {
    return false;
  }
  if (output.state == SolenoidOutputState::Disconnected)
  {
    return true;
  }
  if (_desiredOn(moduleSlot, solenoidIndex))
  {
    return output.state == SolenoidOutputState::On;
  }
  return output.state == SolenoidOutputState::Off;
}

/**
 * Stores a state result.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @param state State reported by the module.
 * @return Nothing.
 */
void SolenoidPoller::_rememberState(uint8_t moduleSlot, uint8_t solenoidIndex,
                                    SolenoidOutputState state)
{
  Output& output = _runtime[moduleSlot].outputs[solenoidIndex];
  output.known = true;
  output.state = state;
  output.revision += 1;
  if (output.revision == 0)
  {
    output.revision = 1;
  }
  if (_intent[moduleSlot].kind == IntentKind::AllOff &&
      state == SolenoidOutputState::On && !_intent[moduleSlot].applying)
  {
    _intent[moduleSlot].pending = true;
  }
}


// ========== Count And Periodic ==========

/**
 * Reads the next due output state.
 *
 * @return True when a periodic query was issued.
 */
bool SolenoidPoller::_servicePeriodic()
{
  const int8_t slot = _periodicSlot();
  if (slot < 0)
  {
    return false;
  }
  SlotRuntime& runtime = _runtime[slot];
  if (!runtime.inCycle)
  {
    runtime.inCycle = true;
    runtime.index = 0;
    runtime.stepAttempts = 0;
  }

  const SolenoidStateResult reading = _moduleHost.querySolenoidState(
      static_cast<uint8_t>(slot), runtime.index);
  if (reading.status == SolenoidQueryStatus::Ok)
  {
    _rememberState(static_cast<uint8_t>(slot), runtime.index, reading.state);
  }
  else
  {
    runtime.stepAttempts = static_cast<uint8_t>(runtime.stepAttempts + 1);
    if (runtime.stepAttempts < kSolenoidQueryAttempts)
    {
      return true;
    }
  }
  runtime.stepAttempts = 0;
  _advancePeriodic(static_cast<uint8_t>(slot));
  return true;
}

/**
 * Finds the slot whose periodic cycle should run.
 *
 * @return Slot index, or -1.
 */
int8_t SolenoidPoller::_periodicSlot() const
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (_runtime[slot].inCycle)
    {
      return static_cast<int8_t>(slot);
    }
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const SlotRuntime& runtime = _runtime[slot];
    if (runtime.countKnown && runtime.count > 0 && _isDue(runtime.nextPollAt))
    {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

/**
 * Reports whether an absolute due time has been reached.
 * A due time of 0 is always due.
 *
 * @param due Absolute monotonic time, or 0.
 * @return True when the time is due.
 */
bool SolenoidPoller::_isDue(uint32_t due) const
{
  if (due == 0)
  {
    return true;
  }
  return static_cast<uint32_t>(_clock.millis() - due) < 0x80000000u;
}

/**
 * Advances the periodic cursor after a finished step.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SolenoidPoller::_advancePeriodic(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.index = static_cast<uint8_t>(runtime.index + 1);
  if (runtime.index >= runtime.count)
  {
    runtime.inCycle = false;
    runtime.index = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
  }
}

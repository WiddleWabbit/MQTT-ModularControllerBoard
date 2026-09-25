#include "PumpPoller.h"

// ========== Construction ==========

PumpPoller::PumpPoller(ModuleHost& moduleHost, IClock& clock,
                       uint32_t pollIntervalMs, uint32_t commandTimeoutMs)
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
 * Issues at most one pump query. No-op when nothing is due.
 * Call after ModuleHost::update() returns.
 *
 * @return Nothing.
 */
void PumpPoller::update()
{
  _syncSessions();
  _armSilence();
  if (_serviceUnknown())
  {
    return;
  }
  if (_serviceReset())
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
 * Records the desired on/off state for one module. Does not touch
 * I2C. Restarts the command-absence timer. A later command for the
 * same slot replaces this one.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param on True commands on.
 * @return False when the slot is out of range.
 */
bool PumpPoller::commandState(uint8_t moduleSlot, bool on)
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return false;
  }
  _silenceStarted = true;
  _silenceAnchor = _clock.millis();
  _failsafeActive = false;
  Intent& intent = _intent[moduleSlot];
  intent.desiredValid = true;
  intent.desiredOn = on;
  intent.applyPending = true;
  intent.applyAttempts = 0;
  return true;
}

/**
 * Records a reset for one module. Does not touch I2C and does not
 * restart the command-absence timer. The desired on/off state is
 * left as it was.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return False when the slot is out of range.
 */
bool PumpPoller::commandReset(uint8_t moduleSlot)
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return false;
  }
  Intent& intent = _intent[moduleSlot];
  intent.resetPending = true;
  intent.resetAttempts = 0;
  return true;
}

/**
 * Reports whether the pump state for a slot is known.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True after a successful state query for the current identity.
 */
bool PumpPoller::stateKnown(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return false;
  }
  return _runtime[moduleSlot].stateKnown;
}

/**
 * Reads the cached pump state.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param state Set when a state has been stored.
 * @return False when no state has been stored.
 */
bool PumpPoller::pumpState(uint8_t moduleSlot, PumpState* state) const
{
  if (state == nullptr || moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].stateKnown)
  {
    return false;
  }
  *state = _runtime[moduleSlot].state;
  return true;
}

/**
 * Reads how many states have been stored for one pump.
 * The value increases each time a state is stored, including when
 * the state is unchanged. Zero means no state is stored.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return State generation, or 0 when none has been stored.
 */
uint32_t PumpPoller::stateRevision(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return 0;
  }
  return _runtime[moduleSlot].revision;
}

/**
 * Reads the poll interval in use.
 *
 * @return Poll interval in milliseconds.
 */
uint32_t PumpPoller::pollIntervalMs() const
{
  return _intervalMs;
}

/**
 * Reads the command-absence timeout in use.
 *
 * @return Timeout in milliseconds.
 */
uint32_t PumpPoller::commandTimeoutMs() const
{
  return _commandTimeoutMs;
}


// ========== Session Sync ==========

/**
 * Forgets a cached state when a slot is not an Online Pump, and
 * when its identity epoch changes. Drops a command when the slot
 * can never be a Pump module.
 *
 * @return Nothing.
 */
void PumpPoller::_syncSessions()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (!_isActivePump(slot))
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
      _intent[slot].applyAttempts = 0;
      _intent[slot].resetAttempts = 0;
      if (saved.desiredValid)
      {
        _intent[slot].applyPending = true;
      }
    }
  }
}

/**
 * Clears one slot's cached state and poll timer.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void PumpPoller::_clearCache(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.epoch = 0;
  runtime.stateKnown = false;
  runtime.state = PumpState::Off;
  runtime.revision = 0;
  runtime.readAttempts = 0;
  runtime.readRetryAt = 0;
  runtime.nextPollAt = 0;
}

/**
 * Forgets the desired state and any pending reset for one slot.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void PumpPoller::_clearIntent(uint8_t moduleSlot)
{
  Intent& intent = _intent[moduleSlot];
  intent.desiredValid = false;
  intent.desiredOn = false;
  intent.applyPending = false;
  intent.applyAttempts = 0;
  intent.resetPending = false;
  intent.resetAttempts = 0;
}

/**
 * Starts the silence window on the first pass, and commands every
 * pump off once the window elapses.
 *
 * @return Nothing.
 */
void PumpPoller::_armSilence()
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
    _armOff(slot);
  }
}

/**
 * Marks one slot to be driven off. Does not restart the silence
 * window and does not cancel a pending reset.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void PumpPoller::_armOff(uint8_t moduleSlot)
{
  Intent& intent = _intent[moduleSlot];
  if (intent.desiredValid && !intent.desiredOn)
  {
    return;
  }
  intent.desiredValid = true;
  intent.desiredOn = false;
  intent.applyPending = true;
  intent.applyAttempts = 0;
}

/**
 * Reports whether a slot is an Online Pump module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True when pump queries are allowed.
 */
bool PumpPoller::_isActivePump(uint8_t moduleSlot) const
{
  return _moduleHost.state(moduleSlot) == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) == module_protocol::kTypePumpModule;
}

/**
 * Reports whether a slot can never host a Pump module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True for Empty, Fault, Unsupported, or a different type.
 */
bool PumpPoller::_intentTargetIsTerminal(uint8_t moduleSlot) const
{
  const SlotState state = _moduleHost.state(moduleSlot);
  if (state == SlotState::Empty || state == SlotState::Fault ||
      state == SlotState::Unsupported)
  {
    return true;
  }
  return state == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) != module_protocol::kTypePumpModule;
}


// ========== Commands ==========

/**
 * Reads the pump state for one module that does not have one yet.
 *
 * @return True when a state query was issued.
 */
bool PumpPoller::_serviceUnknown()
{
  const int8_t slot = _slotNeedingState();
  if (slot < 0)
  {
    return false;
  }
  SlotRuntime& runtime = _runtime[slot];
  const PumpStateResult reading =
      _moduleHost.queryPumpState(static_cast<uint8_t>(slot));
  if (reading.status == PumpQueryStatus::Ok)
  {
    _rememberState(static_cast<uint8_t>(slot), reading.state, true);
    runtime.readAttempts = 0;
    runtime.readRetryAt = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
    return true;
  }

  runtime.readAttempts = static_cast<uint8_t>(runtime.readAttempts + 1);
  if (runtime.readAttempts >= kPumpQueryAttempts)
  {
    runtime.readAttempts = 0;
    runtime.readRetryAt = _clock.millis() + _intervalMs;
  }
  return true;
}

/**
 * Sends one pending reset.
 *
 * @return True when a reset was issued.
 */
bool PumpPoller::_serviceReset()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    Intent& intent = _intent[slot];
    if (!intent.resetPending || !_isActivePump(slot) ||
        !_runtime[slot].stateKnown)
    {
      continue;
    }
    const PumpStateResult reset = _moduleHost.resetPump(slot);
    if (reset.status == PumpQueryStatus::Ok)
    {
      _rememberState(slot, reset.state, true);
      intent.resetPending = false;
      intent.resetAttempts = 0;
      return true;
    }
    intent.resetAttempts = static_cast<uint8_t>(intent.resetAttempts + 1);
    if (intent.resetAttempts >= kPumpQueryAttempts)
    {
      intent.resetPending = false;
      intent.resetAttempts = 0;
    }
    return true;
  }
  return false;
}

/**
 * Drives one pump toward the desired state. Skips a pump that
 * already matches, and a pump whose known state is fault.
 *
 * @return True when a set was issued.
 */
bool PumpPoller::_serviceApply()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    Intent& intent = _intent[slot];
    SlotRuntime& runtime = _runtime[slot];
    if (!intent.applyPending || intent.resetPending || !_isActivePump(slot) ||
        !runtime.stateKnown)
    {
      continue;
    }
    if (runtime.state == PumpState::Fault)
    {
      continue;
    }
    if (_matches(slot))
    {
      intent.applyPending = false;
      intent.applyAttempts = 0;
      continue;
    }

    const PumpStateResult written = _moduleHost.setPump(slot, intent.desiredOn);
    if (written.status == PumpQueryStatus::Ok)
    {
      _rememberState(slot, written.state, false);
      if (written.state == PumpState::Fault || _matches(slot))
      {
        intent.applyPending = false;
        intent.applyAttempts = 0;
      }
      else
      {
        intent.applyAttempts = static_cast<uint8_t>(intent.applyAttempts + 1);
        if (intent.applyAttempts >= kPumpQueryAttempts)
        {
          intent.applyPending = false;
          intent.applyAttempts = 0;
        }
      }
    }
    else
    {
      intent.applyAttempts = static_cast<uint8_t>(intent.applyAttempts + 1);
      if (intent.applyAttempts >= kPumpQueryAttempts)
      {
        intent.applyPending = false;
        intent.applyAttempts = 0;
      }
    }
    return true;
  }
  return false;
}

/**
 * Finds a slot whose first state query is due, preferring a
 * commanded slot.
 *
 * @return Slot index, or -1.
 */
int8_t PumpPoller::_slotNeedingState() const
{
  for (uint8_t pass = 0; pass < 2; ++pass)
  {
    for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
    {
      const Intent& intent = _intent[slot];
      const SlotRuntime& runtime = _runtime[slot];
      if (!_isActivePump(slot) || runtime.stateKnown ||
          !_isDue(runtime.readRetryAt))
      {
        continue;
      }
      const bool commanded =
          intent.resetPending || intent.applyPending || intent.desiredValid;
      if (pass == 0 && !commanded)
      {
        continue;
      }
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

/**
 * Reports whether a known state already satisfies the desired state.
 * A fault does not match, because on and off are not sent while
 * the pump is faulted.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True when no SET is required.
 */
bool PumpPoller::_matches(uint8_t moduleSlot) const
{
  const SlotRuntime& runtime = _runtime[moduleSlot];
  const Intent& intent = _intent[moduleSlot];
  if (!runtime.stateKnown || !intent.desiredValid ||
      runtime.state == PumpState::Fault)
  {
    return false;
  }
  if (intent.desiredOn)
  {
    return runtime.state == PumpState::On;
  }
  return runtime.state == PumpState::Off;
}

/**
 * Stores a state result. A poll that disagrees with the desired
 * state arms another set, except while the pump is faulted.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param state State reported by the module.
 * @param fromPoll True when this store came from a read or a reset,
 *        so a mismatch can arm a set.
 * @return Nothing.
 */
void PumpPoller::_rememberState(uint8_t moduleSlot, PumpState state,
                                bool fromPoll)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.stateKnown = true;
  runtime.state = state;
  runtime.revision += 1;
  if (runtime.revision == 0)
  {
    runtime.revision = 1;
  }
  if (!fromPoll || !_intent[moduleSlot].desiredValid ||
      state == PumpState::Fault)
  {
    return;
  }
  if (!_matches(moduleSlot))
  {
    _intent[moduleSlot].applyPending = true;
    _intent[moduleSlot].applyAttempts = 0;
  }
}


// ========== Periodic Read ==========

/**
 * Reads one due pump state.
 *
 * @return True when a periodic query was issued.
 */
bool PumpPoller::_servicePeriodic()
{
  const int8_t slot = _periodicSlot();
  if (slot < 0)
  {
    return false;
  }
  SlotRuntime& runtime = _runtime[slot];
  const PumpStateResult reading =
      _moduleHost.queryPumpState(static_cast<uint8_t>(slot));
  if (reading.status == PumpQueryStatus::Ok)
  {
    _rememberState(static_cast<uint8_t>(slot), reading.state, true);
    runtime.readAttempts = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
    return true;
  }
  runtime.readAttempts = static_cast<uint8_t>(runtime.readAttempts + 1);
  if (runtime.readAttempts >= kPumpQueryAttempts)
  {
    runtime.readAttempts = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
  }
  return true;
}

/**
 * Finds the slot whose periodic read should run.
 *
 * @return Slot index, or -1.
 */
int8_t PumpPoller::_periodicSlot() const
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const SlotRuntime& runtime = _runtime[slot];
    if (_isActivePump(slot) && runtime.stateKnown && runtime.nextPollAt != 0 &&
        _isDue(runtime.nextPollAt))
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
bool PumpPoller::_isDue(uint32_t due) const
{
  if (due == 0)
  {
    return true;
  }
  return static_cast<uint32_t>(_clock.millis() - due) < 0x80000000u;
}

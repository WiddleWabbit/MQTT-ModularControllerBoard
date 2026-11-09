#include "SensorPoller.h"

// ========== Construction ==========

SensorPoller::SensorPoller(ModuleHost& moduleHost, IClock& clock,
                           uint32_t pollIntervalMs)
  : _moduleHost(moduleHost),
    _clock(clock),
    _intervalMs(pollIntervalMs == 0 ? kDefaultPollIntervalMs : pollIntervalMs),
    _demandHead(0),
    _demandCount(0),
    _resultHead(0),
    _resultCount(0)
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _clearCache(slot);
    _clearIntent(slot);
  }
}


// ========== Public API ==========

/**
 * Issues at most one sensor query. No-op when nothing is due.
 * Call after ModuleHost::update() returns.
 *
 * @return Nothing.
 */
void SensorPoller::update()
{
  _syncSessions();
  _forgetInputsPastCount();
  if (_serviceCount())
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
  if (_serviceConfirm())
  {
    return;
  }
  if (_serviceReadNow())
  {
    return;
  }
  if (_serviceUnknown())
  {
    return;
  }
  if (_serviceDemand())
  {
    return;
  }
  _servicePeriodic();
}

/**
 * Records the desired on/off state for one input. Does not touch I2C.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @param on True commands the loop closed.
 * @return False when the slot or index is out of range.
 */
bool SensorPoller::commandState(uint8_t moduleSlot, uint8_t sensorIndex,
                                bool on)
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return false;
  }
  Intent& intent = _intent[moduleSlot][sensorIndex];
  intent.desiredValid = true;
  intent.desiredOn = on;
  intent.applyPending = true;
  intent.applyAttempts = 0;
  intent.confirmPending = false;
  intent.confirmReads = false;
  intent.confirmAttempts = 0;
  intent.confirmRetryAt = 0;
  intent.readNow = false;
  intent.readAttempts = 0;
  return true;
}

/**
 * Records a reset for one input. Does not touch I2C.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return False when the slot or index is out of range.
 */
bool SensorPoller::commandReset(uint8_t moduleSlot, uint8_t sensorIndex)
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return false;
  }
  Intent& intent = _intent[moduleSlot][sensorIndex];
  intent.resetPending = true;
  intent.resetAttempts = 0;
  return true;
}

/**
 * Queues an on-demand reading. Does not touch I2C.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return False when the slot or index is out of range or the
 *         queue is full.
 */
bool SensorPoller::requestReading(uint8_t moduleSlot, uint8_t sensorIndex)
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      sensorIndex >= module_protocol::kMaxSensorsPerModule ||
      _demandCount >= kDemandQueueDepth)
  {
    return false;
  }
  const uint8_t tail = static_cast<uint8_t>(
      (_demandHead + _demandCount) % kDemandQueueDepth);
  _demands[tail].moduleSlot = moduleSlot;
  _demands[tail].sensorIndex = sensorIndex;
  _demands[tail].attempts = 0;
  _demands[tail].staleReading = false;
  _demandCount = static_cast<uint8_t>(_demandCount + 1);
  return true;
}

/**
 * Reports whether the input count for a slot is known.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True after a successful count query for the current identity.
 */
bool SensorPoller::countKnown(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount)
  {
    return false;
  }
  return _runtime[moduleSlot].countKnown;
}

/**
 * Reads the cached input count.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Count, or 0 when it is not known.
 */
uint8_t SensorPoller::sensorCount(uint8_t moduleSlot) const
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].countKnown)
  {
    return 0;
  }
  return _runtime[moduleSlot].count;
}

/**
 * Reads the cached state for one input.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param state Set when a state has been stored.
 * @return False when the state has not been read yet.
 */
bool SensorPoller::sensorState(uint8_t moduleSlot, uint8_t sensorIndex,
                               SensorState* state) const
{
  if (state == nullptr || moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].countKnown ||
      sensorIndex >= _runtime[moduleSlot].count)
  {
    return false;
  }
  const Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  if (!sample.stateKnown)
  {
    return false;
  }
  *state = sample.state;
  return true;
}

/**
 * Reads the cached sample for one input.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param connected Set when the state has been read.
 * @param hasValue Set true when a reading has been stored.
 * @param value Set when hasValue is true.
 * @return False when the state has not been read yet.
 */
bool SensorPoller::sensorSample(uint8_t moduleSlot, uint8_t sensorIndex,
                                bool* connected, bool* hasValue,
                                int32_t* value) const
{
  SensorState state = SensorState::Off;
  if (connected == nullptr || hasValue == nullptr || value == nullptr ||
      !sensorState(moduleSlot, sensorIndex, &state))
  {
    return false;
  }
  const Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  *connected = state == SensorState::On;
  *hasValue = sample.valueKnown;
  *value = sample.value;
  return true;
}

/**
 * Reads how many publishable observations have been stored.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @return Observation generation, or 0 when none has been stored.
 */
uint32_t SensorPoller::sampleRevision(uint8_t moduleSlot,
                                      uint8_t sensorIndex) const
{
  if (moduleSlot >= module_protocol::kSlotCount ||
      sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return 0;
  }
  return _runtime[moduleSlot].samples[sensorIndex].revision;
}

/**
 * Removes the next on-demand result.
 *
 * @param out Destination. Unchanged when the queue is empty.
 * @return True when a result was copied.
 */
bool SensorPoller::takeDemandResult(SensorDemandResult* out)
{
  if (out == nullptr || _resultCount == 0)
  {
    return false;
  }
  *out = _results[_resultHead];
  _resultHead = static_cast<uint8_t>((_resultHead + 1) % kDemandQueueDepth);
  _resultCount = static_cast<uint8_t>(_resultCount - 1);
  return true;
}


// ========== Session Sync ==========

/**
 * Forgets cached inputs when a slot is not an Online Sensor, and
 * when its identity epoch changes. Drops desired state when the
 * slot can never be a Sensor module.
 *
 * @return Nothing.
 */
void SensorPoller::_syncSessions()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (!_isActiveSensor(slot))
    {
      _clearCache(slot);
      if (_demandTargetIsTerminal(slot))
      {
        _clearIntent(slot);
      }
      continue;
    }
    const uint32_t epoch = _moduleHost.identityEpoch(slot);
    if (_runtime[slot].epoch != epoch)
    {
      Intent saved[module_protocol::kMaxSensorsPerModule];
      for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
           ++index)
      {
        saved[index] = _intent[slot][index];
      }
      _clearCache(slot);
      _runtime[slot].epoch = epoch;
      for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
           ++index)
      {
        _intent[slot][index] = saved[index];
        _intent[slot][index].applyAttempts = 0;
        _intent[slot][index].resetAttempts = 0;
        _intent[slot][index].confirmPending = false;
        _intent[slot][index].confirmReads = false;
        _intent[slot][index].confirmAttempts = 0;
        _intent[slot][index].confirmRetryAt = 0;
        _intent[slot][index].readNow = false;
        _intent[slot][index].readAttempts = 0;
        if (saved[index].desiredValid)
        {
          _intent[slot][index].applyPending = true;
        }
      }
    }
  }
}

/**
 * Clears one slot's cached count, cycle, and samples.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SensorPoller::_clearCache(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.epoch = 0;
  runtime.countKnown = false;
  runtime.count = 0;
  runtime.countAttempts = 0;
  runtime.countRetryAt = 0;
  runtime.inCycle = false;
  runtime.index = 0;
  runtime.readingStep = false;
  runtime.stepAttempts = 0;
  runtime.nextPollAt = 0;
  for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
       ++index)
  {
    Sample& sample = runtime.samples[index];
    sample.stateKnown = false;
    sample.state = SensorState::Off;
    sample.valueKnown = false;
    sample.value = 0;
    sample.revision = 0;
    sample.fetchAttempts = 0;
    sample.fetchRetryAt = 0;
  }
}

/**
 * Drops commands for inputs the module does not report.
 *
 * @return Nothing.
 */
void SensorPoller::_forgetInputsPastCount()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (!_runtime[slot].countKnown)
    {
      continue;
    }
    for (uint8_t index = _runtime[slot].count;
         index < module_protocol::kMaxSensorsPerModule; ++index)
    {
      Intent& intent = _intent[slot][index];
      intent.desiredValid = false;
      intent.desiredOn = false;
      intent.applyPending = false;
      intent.applyAttempts = 0;
      intent.resetPending = false;
      intent.resetAttempts = 0;
      intent.confirmPending = false;
      intent.confirmReads = false;
      intent.confirmAttempts = 0;
      intent.confirmRetryAt = 0;
      intent.readNow = false;
      intent.readAttempts = 0;
    }
  }
}

/**
 * Forgets desired state, resets, and follow-up reads for one slot.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SensorPoller::_clearIntent(uint8_t moduleSlot)
{
  for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
       ++index)
  {
    Intent& intent = _intent[moduleSlot][index];
    intent.desiredValid = false;
    intent.desiredOn = false;
    intent.applyPending = false;
    intent.applyAttempts = 0;
    intent.resetPending = false;
    intent.resetAttempts = 0;
    intent.confirmPending = false;
    intent.confirmReads = false;
    intent.confirmAttempts = 0;
    intent.confirmRetryAt = 0;
    intent.readNow = false;
    intent.readAttempts = 0;
  }
}

/**
 * Reports whether a slot is an Online Sensor module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True when sensor queries are allowed.
 */
bool SensorPoller::_isActiveSensor(uint8_t moduleSlot) const
{
  return _moduleHost.state(moduleSlot) == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) == module_protocol::kTypeSensorModule;
}

/**
 * Reports whether a slot can never host a Sensor module.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return True for Empty, Fault, Unsupported, or a different type.
 */
bool SensorPoller::_demandTargetIsTerminal(uint8_t moduleSlot) const
{
  const SlotState state = _moduleHost.state(moduleSlot);
  if (state == SlotState::Empty || state == SlotState::Fault ||
      state == SlotState::Unsupported)
  {
    return true;
  }
  return state == SlotState::Online &&
         _moduleHost.typeId(moduleSlot) != module_protocol::kTypeSensorModule;
}

/**
 * Reports whether one input can be queried now.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @return True when the slot is an online sensor and the index is
 *         inside the known count.
 */
bool SensorPoller::_inputReady(uint8_t moduleSlot, uint8_t sensorIndex) const
{
  return _isActiveSensor(moduleSlot) && _runtime[moduleSlot].countKnown &&
         sensorIndex < _runtime[moduleSlot].count;
}


// ========== Commands ==========

/**
 * Sends one pending reset whose state is already known.
 *
 * @return True when a reset was issued.
 */
bool SensorPoller::_serviceReset()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      Intent& intent = _intent[slot][index];
      if (!intent.resetPending || !_inputReady(slot, index) ||
          !_runtime[slot].samples[index].stateKnown)
      {
        continue;
      }
      const SensorStateResult reset = resetSensor(_moduleHost, slot, index);
      if (reset.status == SensorQueryStatus::Ok)
      {
        _rememberState(slot, index, reset.state, true);
        intent.resetPending = false;
        intent.resetAttempts = 0;
        return true;
      }
      intent.resetAttempts = static_cast<uint8_t>(intent.resetAttempts + 1);
      if (intent.resetAttempts >= kSensorQueryAttempts)
      {
        intent.resetPending = false;
        intent.resetAttempts = 0;
      }
      return true;
    }
  }
  return false;
}

/**
 * Drives one input toward the desired state, or arms a state
 * confirm when no SET is required. Skips a fault.
 *
 * @return True when a SET was issued.
 */
bool SensorPoller::_serviceApply()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      Intent& intent = _intent[slot][index];
      Sample& sample = _runtime[slot].samples[index];
      if (!intent.applyPending || intent.resetPending ||
          !_inputReady(slot, index) || !sample.stateKnown)
      {
        continue;
      }
      if (sample.state == SensorState::Fault || _matches(slot, index))
      {
        intent.applyPending = false;
        intent.applyAttempts = 0;
        _armConfirm(slot, index,
                    intent.desiredOn && sample.state != SensorState::Fault);
        continue;
      }

      const SensorStateResult written =
          setSensor(_moduleHost, slot, index, intent.desiredOn);
      if (written.status == SensorQueryStatus::Busy)
      {
        intent.applyPending = false;
        intent.applyAttempts = 0;
        _armConfirm(slot, index, intent.desiredOn);
        return true;
      }
      if (written.status == SensorQueryStatus::Ok)
      {
        _rememberState(slot, index, written.state, false);
        intent.applyPending = false;
        intent.applyAttempts = 0;
        _armConfirm(slot, index, intent.desiredOn);
        return true;
      }
      intent.applyAttempts = static_cast<uint8_t>(intent.applyAttempts + 1);
      if (intent.applyAttempts >= kSensorQueryAttempts)
      {
        intent.applyPending = false;
        intent.applyAttempts = 0;
      }
      return true;
    }
  }
  return false;
}

/**
 * Reads the state of an input that was just switched, or that an
 * on/off command asked to confirm.
 *
 * @return True when a state query was issued.
 */
bool SensorPoller::_serviceConfirm()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      Intent& intent = _intent[slot][index];
      if (!intent.confirmPending || !_inputReady(slot, index) ||
          !_isDue(intent.confirmRetryAt))
      {
        continue;
      }
      const bool readAfter = intent.confirmReads;
      const SensorStateResult result =
          querySensorState(_moduleHost, slot, index);
      if (result.status == SensorQueryStatus::Ok)
      {
        _rememberState(slot, index, result.state, false);
        intent.confirmPending = false;
        intent.confirmAttempts = 0;
        intent.confirmRetryAt = 0;
        intent.confirmReads = false;
        if (readAfter && result.state == SensorState::On)
        {
          intent.readNow = true;
          intent.readAttempts = 0;
        }
        return true;
      }
      intent.confirmAttempts =
          static_cast<uint8_t>(intent.confirmAttempts + 1);
      if (intent.confirmAttempts >= kSensorQueryAttempts)
      {
        intent.confirmAttempts = 0;
        intent.confirmRetryAt = _clock.millis() + _intervalMs;
      }
      return true;
    }
  }
  return false;
}

/**
 * Reads an input whose confirm just reported on.
 *
 * @return True when a reading was issued.
 */
bool SensorPoller::_serviceReadNow()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      Intent& intent = _intent[slot][index];
      if (!intent.readNow || !_inputReady(slot, index))
      {
        continue;
      }
      const Sample& sample = _runtime[slot].samples[index];
      if (!sample.stateKnown || sample.state != SensorState::On)
      {
        intent.readNow = false;
        intent.readAttempts = 0;
        continue;
      }
      const SensorReadingResult reading =
          querySensorReading(_moduleHost, slot, index);
      if (reading.status == SensorQueryStatus::Ok && reading.connected)
      {
        _rememberReading(slot, index, reading.value);
        intent.readNow = false;
        intent.readAttempts = 0;
        return true;
      }
      if (reading.status == SensorQueryStatus::Ok)
      {
        intent.readNow = false;
        intent.readAttempts = 0;
        _armConfirm(slot, index, false);
        return true;
      }
      intent.readAttempts = static_cast<uint8_t>(intent.readAttempts + 1);
      if (intent.readAttempts >= kSensorQueryAttempts)
      {
        intent.readNow = false;
        intent.readAttempts = 0;
      }
      return true;
    }
  }
  return false;
}

/**
 * Reads the state of an input that a command or an immediate read
 * still needs.
 *
 * @return True when a state query was issued.
 */
bool SensorPoller::_serviceUnknown()
{
  uint8_t slot = 0;
  uint8_t index = 0;
  bool found = false;
  for (uint8_t candidate = 0;
       candidate < module_protocol::kSlotCount && !found; ++candidate)
  {
    for (uint8_t sensor = 0;
         sensor < module_protocol::kMaxSensorsPerModule && !found; ++sensor)
    {
      const Intent& intent = _intent[candidate][sensor];
      const Sample& sample = _runtime[candidate].samples[sensor];
      const bool waiting = intent.applyPending || intent.resetPending ||
                           intent.confirmPending;
      if (waiting && _inputReady(candidate, sensor) && !sample.stateKnown &&
          _isDue(sample.fetchRetryAt))
      {
        slot = candidate;
        index = sensor;
        found = true;
      }
    }
  }
  if (!found && _demandCount > 0)
  {
    const DemandRequest& demand = _demands[_demandHead];
    const Sample& sample =
        _runtime[demand.moduleSlot].samples[demand.sensorIndex];
    if (_inputReady(demand.moduleSlot, demand.sensorIndex) &&
        !sample.stateKnown && _isDue(sample.fetchRetryAt))
    {
      slot = demand.moduleSlot;
      index = demand.sensorIndex;
      found = true;
    }
  }
  if (!found)
  {
    return false;
  }

  Sample& sample = _runtime[slot].samples[index];
  const SensorStateResult result = querySensorState(_moduleHost, slot, index);
  if (result.status == SensorQueryStatus::Ok)
  {
    _rememberState(slot, index, result.state, true);
    sample.fetchAttempts = 0;
    sample.fetchRetryAt = 0;
    return true;
  }
  sample.fetchAttempts = static_cast<uint8_t>(sample.fetchAttempts + 1);
  if (sample.fetchAttempts >= kSensorQueryAttempts)
  {
    sample.fetchAttempts = 0;
    sample.fetchRetryAt = _clock.millis() + _intervalMs;
  }
  return true;
}

/**
 * Services the head of the on-demand queue.
 *
 * @return True when this update was consumed.
 */
bool SensorPoller::_serviceDemand()
{
  if (_demandCount == 0)
  {
    return false;
  }
  DemandRequest& demand = _demands[_demandHead];
  if (_demandTargetIsTerminal(demand.moduleSlot))
  {
    _failDemand(demand);
    return true;
  }
  if (!_isActiveSensor(demand.moduleSlot) ||
      !_runtime[demand.moduleSlot].countKnown)
  {
    return false;
  }
  if (demand.sensorIndex >= _runtime[demand.moduleSlot].count)
  {
    _failDemand(demand);
    return true;
  }
  const Sample& sample =
      _runtime[demand.moduleSlot].samples[demand.sensorIndex];
  if (!sample.stateKnown)
  {
    return false;
  }
  if (demand.staleReading)
  {
    const Intent& intent =
        _intent[demand.moduleSlot][demand.sensorIndex];
    if (intent.confirmPending)
    {
      return false;
    }
    demand.staleReading = false;
    if (sample.state == SensorState::On)
    {
      if (!sample.valueKnown)
      {
        _popDemand();
        return true;
      }
      SensorDemandResult result;
      result.moduleSlot = demand.moduleSlot;
      result.sensorIndex = demand.sensorIndex;
      result.ok = true;
      result.state = SensorState::On;
      result.hasValue = true;
      result.value = sample.value;
      _pushResult(result);
      _popDemand();
      return true;
    }
  }
  if (sample.state != SensorState::On)
  {
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = true;
    result.state = sample.state;
    result.hasValue = false;
    result.value = 0;
    _pushResult(result);
    _popDemand();
    return true;
  }

  const SensorReadingResult reading = querySensorReading(
      _moduleHost, demand.moduleSlot, demand.sensorIndex);
  if (reading.status == SensorQueryStatus::Ok && reading.connected)
  {
    _rememberReading(demand.moduleSlot, demand.sensorIndex, reading.value);
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = true;
    result.state = SensorState::On;
    result.hasValue = true;
    result.value = reading.value;
    _pushResult(result);
    _popDemand();
    return true;
  }
  if (reading.status == SensorQueryStatus::Ok)
  {
    _armConfirm(demand.moduleSlot, demand.sensorIndex, false);
    demand.staleReading = true;
    return true;
  }
  if (reading.status == SensorQueryStatus::Rejected)
  {
    _failDemand(demand);
    return true;
  }

  demand.attempts = static_cast<uint8_t>(demand.attempts + 1);
  if (demand.attempts < kSensorQueryAttempts)
  {
    return true;
  }
  _failDemand(demand);
  return true;
}

/**
 * Fills a failed on-demand result and removes the head request.
 *
 * @param demand Head request.
 * @return Nothing.
 */
void SensorPoller::_failDemand(const DemandRequest& demand)
{
  SensorDemandResult result;
  result.moduleSlot = demand.moduleSlot;
  result.sensorIndex = demand.sensorIndex;
  result.ok = false;
  result.state = SensorState::Off;
  result.hasValue = false;
  result.value = 0;
  _pushResult(result);
  _popDemand();
}

/**
 * Appends an on-demand result, dropping the oldest if the queue is full.
 *
 * @param result Completed demand.
 * @return Nothing.
 */
void SensorPoller::_pushResult(const SensorDemandResult& result)
{
  if (_resultCount >= kDemandQueueDepth)
  {
    _resultHead = static_cast<uint8_t>((_resultHead + 1) % kDemandQueueDepth);
    _resultCount = static_cast<uint8_t>(_resultCount - 1);
  }
  const uint8_t tail = static_cast<uint8_t>(
      (_resultHead + _resultCount) % kDemandQueueDepth);
  _results[tail] = result;
  _resultCount = static_cast<uint8_t>(_resultCount + 1);
}

/**
 * Removes the head on-demand request.
 *
 * @return Nothing.
 */
void SensorPoller::_popDemand()
{
  if (_demandCount == 0)
  {
    return;
  }
  _demandHead = static_cast<uint8_t>((_demandHead + 1) % kDemandQueueDepth);
  _demandCount = static_cast<uint8_t>(_demandCount - 1);
}


// ========== Count And Periodic ==========

/**
 * Queries the input count for one module that still needs it.
 *
 * @return True when a count query was issued.
 */
bool SensorPoller::_serviceCount()
{
  const int8_t slot = _slotNeedingCount();
  if (slot < 0)
  {
    return false;
  }
  SlotRuntime& runtime = _runtime[slot];
  const SensorCountResult count = querySensorCount(
      _moduleHost, static_cast<uint8_t>(slot));
  if (count.status == SensorQueryStatus::Ok)
  {
    runtime.countKnown = true;
    runtime.count = count.count;
    runtime.countAttempts = 0;
    runtime.countRetryAt = 0;
    runtime.inCycle = false;
    runtime.index = 0;
    runtime.readingStep = false;
    runtime.stepAttempts = 0;
    runtime.nextPollAt = 0;
    return true;
  }

  runtime.countAttempts = static_cast<uint8_t>(runtime.countAttempts + 1);
  if (runtime.countAttempts >= kSensorQueryAttempts)
  {
    runtime.countAttempts = 0;
    runtime.countRetryAt = _clock.millis() + _intervalMs;
  }
  return true;
}

/**
 * Queries the next due state, or the reading for an input whose
 * state this cycle is on.
 *
 * @return True when a periodic query was issued.
 */
bool SensorPoller::_servicePeriodic()
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
    runtime.readingStep = false;
    runtime.stepAttempts = 0;
  }
  if (runtime.count == 0)
  {
    runtime.inCycle = false;
    return false;
  }

  if (!runtime.readingStep)
  {
    const SensorStateResult result =
        querySensorState(_moduleHost, static_cast<uint8_t>(slot),
                         runtime.index);
    if (result.status == SensorQueryStatus::Ok)
    {
      _rememberState(static_cast<uint8_t>(slot), runtime.index, result.state,
                     true);
      runtime.stepAttempts = 0;
      if (result.state == SensorState::On &&
          _runtime[slot].samples[runtime.index].state == SensorState::On)
      {
        runtime.readingStep = true;
        return true;
      }
      if (_runtime[slot].inCycle && !_runtime[slot].readingStep &&
          _runtime[slot].index == runtime.index)
      {
        _advancePeriodic(static_cast<uint8_t>(slot));
      }
      return true;
    }
  }
  else
  {
    const SensorReadingResult reading = querySensorReading(
        _moduleHost, static_cast<uint8_t>(slot), runtime.index);
    if (reading.status == SensorQueryStatus::Ok && reading.connected)
    {
      _rememberReading(static_cast<uint8_t>(slot), runtime.index,
                       reading.value);
      runtime.stepAttempts = 0;
      runtime.readingStep = false;
      _advancePeriodic(static_cast<uint8_t>(slot));
      return true;
    }
    if (reading.status == SensorQueryStatus::Ok)
    {
      _armConfirm(static_cast<uint8_t>(slot), runtime.index, false);
      runtime.stepAttempts = 0;
      runtime.readingStep = false;
      _advancePeriodic(static_cast<uint8_t>(slot));
      return true;
    }
  }

  runtime.stepAttempts = static_cast<uint8_t>(runtime.stepAttempts + 1);
  if (runtime.stepAttempts < kSensorQueryAttempts)
  {
    return true;
  }
  runtime.stepAttempts = 0;
  runtime.readingStep = false;
  _advancePeriodic(static_cast<uint8_t>(slot));
  return true;
}

/**
 * Finds a slot whose count query is due, preferring a demanded slot.
 *
 * @return Slot index, or -1.
 */
int8_t SensorPoller::_slotNeedingCount() const
{
  if (_demandCount > 0)
  {
    const uint8_t preferred = _demands[_demandHead].moduleSlot;
    const SlotRuntime& runtime = _runtime[preferred];
    if (_isActiveSensor(preferred) && !runtime.countKnown &&
        _isDue(runtime.countRetryAt))
    {
      return static_cast<int8_t>(preferred);
    }
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      const Intent& intent = _intent[slot][index];
      if (!(intent.applyPending || intent.resetPending ||
            intent.confirmPending))
      {
        continue;
      }
      const SlotRuntime& runtime = _runtime[slot];
      if (_isActiveSensor(slot) && !runtime.countKnown &&
          _isDue(runtime.countRetryAt))
      {
        return static_cast<int8_t>(slot);
      }
    }
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const SlotRuntime& runtime = _runtime[slot];
    if (_isActiveSensor(slot) && !runtime.countKnown &&
        _isDue(runtime.countRetryAt))
    {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

/**
 * Finds the slot whose periodic cycle should run.
 *
 * @return Slot index, or -1.
 */
int8_t SensorPoller::_periodicSlot() const
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
bool SensorPoller::_isDue(uint32_t due) const
{
  if (due == 0)
  {
    return true;
  }
  return static_cast<uint32_t>(_clock.millis() - due) < 0x80000000u;
}

/**
 * Reports whether a known state already satisfies the desired state.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @return True when no SET is required.
 */
bool SensorPoller::_matches(uint8_t moduleSlot, uint8_t sensorIndex) const
{
  const Intent& intent = _intent[moduleSlot][sensorIndex];
  const Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  if (!intent.desiredValid || !sample.stateKnown ||
      sample.state == SensorState::Fault)
  {
    return false;
  }
  if (intent.desiredOn)
  {
    return sample.state == SensorState::On ||
           sample.state == SensorState::Disconnected;
  }
  return sample.state == SensorState::Off;
}

/**
 * Arms the state read that follows an on or off command.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param readAfter True when an on result should be followed by a
 *        reading.
 * @return Nothing.
 */
void SensorPoller::_armConfirm(uint8_t moduleSlot, uint8_t sensorIndex,
                               bool readAfter)
{
  Intent& intent = _intent[moduleSlot][sensorIndex];
  intent.confirmPending = true;
  intent.confirmReads = readAfter;
  intent.confirmAttempts = 0;
  intent.confirmRetryAt = 0;
}

/**
 * Stores a state result. A poll that disagrees with the desired
 * state arms a set, except while the input is faulted.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param state State reported by the module.
 * @param fromPoll True when a mismatch should arm a set.
 * @return Nothing.
 */
void SensorPoller::_rememberState(uint8_t moduleSlot, uint8_t sensorIndex,
                                  SensorState state, bool fromPoll)
{
  Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  sample.stateKnown = true;
  sample.state = state;
  if (state != SensorState::On)
  {
    sample.valueKnown = false;
    sample.value = 0;
    _bump(sample);
    _dropPeriodicReading(moduleSlot, sensorIndex);
  }
  if (!fromPoll || state == SensorState::Fault)
  {
    return;
  }
  Intent& intent = _intent[moduleSlot][sensorIndex];
  if (!intent.desiredValid || _matches(moduleSlot, sensorIndex))
  {
    return;
  }
  intent.applyPending = true;
  intent.applyAttempts = 0;
}

/**
 * Stores a reading taken while the input is on.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param value Raw module value.
 * @return Nothing.
 */
void SensorPoller::_rememberReading(uint8_t moduleSlot, uint8_t sensorIndex,
                                    int32_t value)
{
  Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  sample.stateKnown = true;
  sample.state = SensorState::On;
  sample.valueKnown = true;
  sample.value = value;
  _bump(sample);
}

/**
 * Drops a periodic reading that is no longer valid for this input.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @return Nothing.
 */
void SensorPoller::_dropPeriodicReading(uint8_t moduleSlot,
                                        uint8_t sensorIndex)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  if (!runtime.inCycle || !runtime.readingStep ||
      runtime.index != sensorIndex)
  {
    return;
  }
  runtime.readingStep = false;
  runtime.stepAttempts = 0;
  _advancePeriodic(moduleSlot);
}

/**
 * Moves the periodic cursor to the next input.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SensorPoller::_advancePeriodic(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  runtime.readingStep = false;
  runtime.index = static_cast<uint8_t>(runtime.index + 1);
  if (runtime.index >= runtime.count)
  {
    runtime.inCycle = false;
    runtime.index = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
  }
}

/**
 * Increases the publish generation, skipping 0.
 *
 * @param sample Cached input.
 * @return Nothing.
 */
void SensorPoller::_bump(Sample& sample)
{
  sample.revision += 1;
  if (sample.revision == 0)
  {
    sample.revision = 1;
  }
}

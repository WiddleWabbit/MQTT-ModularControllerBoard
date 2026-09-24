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
    _clearSlot(slot);
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
  if (_serviceDemand())
  {
    return;
  }
  if (_serviceCount())
  {
    return;
  }
  _servicePeriodic();
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
 * Reads the cached sample for one input.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param connected Set when presence has been read.
 * @param hasValue Set true when a reading has been stored.
 * @param value Set when hasValue is true.
 * @return False when the count is unknown, the index is outside
 *         that count, or presence has not been read yet.
 */
bool SensorPoller::sensorSample(uint8_t moduleSlot, uint8_t sensorIndex,
                                bool* connected, bool* hasValue,
                                int32_t* value) const
{
  if (connected == nullptr || hasValue == nullptr || value == nullptr ||
      moduleSlot >= module_protocol::kSlotCount ||
      !_runtime[moduleSlot].countKnown ||
      sensorIndex >= _runtime[moduleSlot].count)
  {
    return false;
  }
  const Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  if (!sample.connectedKnown)
  {
    return false;
  }
  *connected = sample.connected;
  *hasValue = sample.valueKnown;
  *value = sample.value;
  return true;
}

/**
 * Reads how many readings have been stored for one input.
 * The value increases each time a reading is stored, including when
 * the raw value is unchanged. Zero means no reading is stored.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @return Reading generation, or 0 when none has been stored.
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
 * when its identity epoch changes.
 *
 * @return Nothing.
 */
void SensorPoller::_syncSessions()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    if (!_isActiveSensor(slot))
    {
      _clearSlot(slot);
      continue;
    }
    const uint32_t epoch = _moduleHost.identityEpoch(slot);
    if (_runtime[slot].epoch != epoch)
    {
      _clearSlot(slot);
      _runtime[slot].epoch = epoch;
    }
  }
}

/**
 * Clears one slot's cached count, cycle, and samples.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SensorPoller::_clearSlot(uint8_t moduleSlot)
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
    runtime.samples[index].connectedKnown = false;
    runtime.samples[index].connected = false;
    runtime.samples[index].valueKnown = false;
    runtime.samples[index].value = 0;
    runtime.samples[index].revision = 0;
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
 * Reports whether an on-demand target can never succeed.
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


// ========== Demand ==========

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
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = false;
    result.connected = false;
    result.value = 0;
    _pushResult(result);
    _popDemand();
    return true;
  }
  if (!_isActiveSensor(demand.moduleSlot) ||
      !_runtime[demand.moduleSlot].countKnown)
  {
    return false;
  }
  if (demand.sensorIndex >= _runtime[demand.moduleSlot].count)
  {
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = false;
    result.connected = false;
    result.value = 0;
    _pushResult(result);
    _popDemand();
    return true;
  }

  const SensorReadingResult reading = _moduleHost.querySensorReading(
      demand.moduleSlot, demand.sensorIndex);
  if (reading.status == SensorQueryStatus::Ok)
  {
    _rememberReading(demand.moduleSlot, demand.sensorIndex, reading.connected,
                     reading.value);
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = true;
    result.connected = reading.connected;
    result.value = reading.value;
    _pushResult(result);
    _popDemand();
    return true;
  }
  if (reading.status == SensorQueryStatus::Rejected)
  {
    SensorDemandResult result;
    result.moduleSlot = demand.moduleSlot;
    result.sensorIndex = demand.sensorIndex;
    result.ok = false;
    result.connected = false;
    result.value = 0;
    _pushResult(result);
    _popDemand();
    return true;
  }

  demand.attempts = static_cast<uint8_t>(demand.attempts + 1);
  if (demand.attempts < kSensorQueryAttempts)
  {
    return true;
  }
  SensorDemandResult result;
  result.moduleSlot = demand.moduleSlot;
  result.sensorIndex = demand.sensorIndex;
  result.ok = false;
  result.connected = false;
  result.value = 0;
  _pushResult(result);
  _popDemand();
  return true;
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
  const SensorCountResult count = _moduleHost.querySensorCount(
      static_cast<uint8_t>(slot));
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
 * Queries the next due presence or reading.
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

  bool succeeded = false;
  if (!runtime.readingStep)
  {
    const SensorConnectedResult presence = _moduleHost.querySensorConnected(
        static_cast<uint8_t>(slot), runtime.index);
    if (presence.status == SensorQueryStatus::Ok)
    {
      _rememberConnected(static_cast<uint8_t>(slot), runtime.index,
                         presence.connected);
      succeeded = true;
    }
  }
  else
  {
    const SensorReadingResult reading = _moduleHost.querySensorReading(
        static_cast<uint8_t>(slot), runtime.index);
    if (reading.status == SensorQueryStatus::Ok)
    {
      _rememberReading(static_cast<uint8_t>(slot), runtime.index,
                       reading.connected, reading.value);
      succeeded = true;
    }
  }

  if (!succeeded)
  {
    runtime.stepAttempts = static_cast<uint8_t>(runtime.stepAttempts + 1);
    if (runtime.stepAttempts < kSensorQueryAttempts)
    {
      return true;
    }
  }
  runtime.stepAttempts = 0;
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
 * Stores a presence result.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param connected Presence flag.
 * @return Nothing.
 */
void SensorPoller::_rememberConnected(uint8_t moduleSlot, uint8_t sensorIndex,
                                      bool connected)
{
  Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  sample.connectedKnown = true;
  sample.connected = connected;
}

/**
 * Stores a reading result, including its connected flag.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param connected Presence flag from the reading.
 * @param value Raw module value.
 * @return Nothing.
 */
void SensorPoller::_rememberReading(uint8_t moduleSlot, uint8_t sensorIndex,
                                    bool connected, int32_t value)
{
  Sample& sample = _runtime[moduleSlot].samples[sensorIndex];
  sample.connectedKnown = true;
  sample.connected = connected;
  sample.valueKnown = true;
  sample.value = value;
  sample.revision += 1;
  if (sample.revision == 0)
  {
    sample.revision = 1;
  }
}

/**
 * Advances the periodic cursor after a finished step.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @return Nothing.
 */
void SensorPoller::_advancePeriodic(uint8_t moduleSlot)
{
  SlotRuntime& runtime = _runtime[moduleSlot];
  if (!runtime.readingStep)
  {
    runtime.readingStep = true;
    return;
  }
  runtime.readingStep = false;
  runtime.index = static_cast<uint8_t>(runtime.index + 1);
  if (runtime.index >= runtime.count)
  {
    runtime.inCycle = false;
    runtime.index = 0;
    runtime.nextPollAt = _clock.millis() + _intervalMs;
  }
}

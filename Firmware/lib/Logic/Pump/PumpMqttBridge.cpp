#include "PumpMqttBridge.h"

#include <cstdio>
#include <cstring>

// ========== Construction ==========

PumpMqttBridge::PumpMqttBridge(PumpPoller& poller, MqttService& mqttService,
                               MqttTopicLayout& topics)
  : _poller(poller),
    _mqttService(mqttService),
    _topics(topics),
    _seenGeneration(topics.generation())
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _publishedOk[slot] = false;
    _publishedRevision[slot] = 0;
    _published[slot][0] = '\0';
    for (uint8_t record = 0; record < kRecordCount; ++record)
    {
      _recordOk[slot][record] = false;
      _recordRevision[slot][record] = 0;
      _recordUnavailable[slot][record] = false;
    }
  }
}


// ========== Public API ==========

/**
 * Records a desired state or a reset when the topic and payload
 * name one module. Ignores every other message. Safe to call from
 * the MQTT callback.
 *
 * @param topic Received topic.
 * @param payload Payload bytes. Not necessarily NUL-terminated.
 * @param length Payload length.
 * @param context PumpMqttBridge instance.
 * @return Nothing.
 */
void PumpMqttBridge::onMqttMessage(const char* topic, const uint8_t* payload,
                                   size_t length, void* context)
{
  PumpMqttBridge* bridge = static_cast<PumpMqttBridge*>(context);
  if (bridge == nullptr)
  {
    return;
  }
  bridge->_handleMessage(topic, payload, length);
}

/**
 * Publishes each new state with retain off.
 *
 * @return Nothing.
 */
void PumpMqttBridge::update()
{
  _syncTopicGeneration();
  _publishSnapshots();
  _publishRecords();
}


/**
 * Forgets accepted states when the device id has changed.
 *
 * @return Nothing.
 */
void PumpMqttBridge::_syncTopicGeneration()
{
  const uint32_t generation = _topics.generation();
  if (generation == _seenGeneration)
  {
    return;
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _publishedOk[slot] = false;
    _publishedRevision[slot] = 0;
    _published[slot][0] = '\0';
    for (uint8_t record = 0; record < kRecordCount; ++record)
    {
      _recordOk[slot][record] = false;
      _recordRevision[slot][record] = 0;
      _recordUnavailable[slot][record] = false;
    }
  }
  _seenGeneration = generation;
}


// ========== Commands ==========

namespace
{
/**
 * One accepted pump command verb.
 */
enum class PumpVerb : uint8_t
{
  On,
  Off,
  Reset
};

/**
 * Skips spaces and tabs.
 *
 * @param cursor Current position. Advanced to the next token or end.
 * @param end One past the last payload byte.
 * @return Nothing.
 */
void skipSpace(const char*& cursor, const char* end)
{
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
}

/**
 * Parses one unsigned decimal token.
 *
 * @param cursor Current position. Advanced past the token on success.
 * @param end One past the last payload byte.
 * @param value Parsed value.
 * @return False when the cursor is not on a digit.
 */
bool readUnsigned(const char*& cursor, const char* end, unsigned* value)
{
  if (cursor >= end || *cursor < '0' || *cursor > '9')
  {
    return false;
  }
  unsigned parsed = 0;
  while (cursor < end && *cursor >= '0' && *cursor <= '9')
  {
    parsed = (parsed * 10u) + static_cast<unsigned>(*cursor - '0');
    if (parsed > module_protocol::kSlotCount)
    {
      return false;
    }
    ++cursor;
  }
  *value = parsed;
  return true;
}

/**
 * Parses one "on", "off", or "reset" token.
 *
 * @param cursor Current position. Advanced past the token on success.
 * @param end One past the last payload byte.
 * @param verb Parsed verb.
 * @return False when the next token is none of those words.
 */
bool readVerb(const char*& cursor, const char* end, PumpVerb* verb)
{
  const size_t remaining = static_cast<size_t>(end - cursor);
  if (remaining >= 2 && cursor[0] == 'o' && cursor[1] == 'n' &&
      (remaining == 2 || cursor[2] == ' ' || cursor[2] == '\t'))
  {
    *verb = PumpVerb::On;
    cursor += 2;
    return true;
  }
  if (remaining >= 3 && cursor[0] == 'o' && cursor[1] == 'f' &&
      cursor[2] == 'f' &&
      (remaining == 3 || cursor[3] == ' ' || cursor[3] == '\t'))
  {
    *verb = PumpVerb::Off;
    cursor += 3;
    return true;
  }
  if (remaining >= 5 && cursor[0] == 'r' && cursor[1] == 'e' &&
      cursor[2] == 's' && cursor[3] == 'e' && cursor[4] == 't' &&
      (remaining == 5 || cursor[5] == ' ' || cursor[5] == '\t'))
  {
    *verb = PumpVerb::Reset;
    cursor += 5;
    return true;
  }
  return false;
}
}

/**
 * Parses a pump command and records it.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void PumpMqttBridge::_handleMessage(const char* topic, const uint8_t* payload,
                                    size_t length)
{
  if (topic == nullptr || std::strcmp(topic, _topics.pumpCommand()) != 0)
  {
    return;
  }
  if (payload == nullptr && length > 0)
  {
    return;
  }
  const char* cursor = reinterpret_cast<const char*>(payload);
  const char* end = cursor + length;
  skipSpace(cursor, end);
  unsigned moduleNumber = 0;
  if (!readUnsigned(cursor, end, &moduleNumber))
  {
    return;
  }
  if (moduleNumber < 1 || moduleNumber > module_protocol::kSlotCount)
  {
    return;
  }
  if (cursor >= end || (*cursor != ' ' && *cursor != '\t'))
  {
    return;
  }
  skipSpace(cursor, end);
  PumpVerb verb = PumpVerb::Off;
  if (!readVerb(cursor, end, &verb))
  {
    return;
  }
  skipSpace(cursor, end);
  if (cursor != end)
  {
    return;
  }
  const uint8_t moduleSlot = static_cast<uint8_t>(moduleNumber - 1);
  if (verb == PumpVerb::Reset)
  {
    _poller.commandReset(moduleSlot);
    return;
  }
  _poller.commandState(moduleSlot, verb == PumpVerb::On);
}


// ========== Publication ==========

/**
 * Publishes each stored state that has not been published yet.
 *
 * @return Nothing.
 */
void PumpMqttBridge::_publishSnapshots()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    char payload[16];
    if (!_poller.stateKnown(slot) || !_formatState(slot, payload, sizeof(payload)))
    {
      if (_publishedOk[slot] &&
          std::strcmp(_published[slot], "unavailable") != 0)
      {
        if (_publish(slot, "unavailable"))
        {
          std::snprintf(_published[slot], sizeof(_published[slot]),
                        "unavailable");
        }
      }
      continue;
    }
    const uint32_t revision = _poller.stateRevision(slot);
    if (revision == 0 || revision == _publishedRevision[slot])
    {
      continue;
    }
    if (!_publish(slot, payload))
    {
      continue;
    }
    std::snprintf(_published[slot], sizeof(_published[slot]), "%s", payload);
    _publishedOk[slot] = true;
    _publishedRevision[slot] = revision;
  }
}

/**
 * Publishes one pump topic with retain off.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param payload Text payload.
 * @return True when publication was accepted.
 */
bool PumpMqttBridge::_publish(uint8_t moduleSlot, const char* payload)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/pump", _topics.slotPrefix(),
                moduleNumber);
  return _mqttService.publish(topic, payload, false);
}

/**
 * Formats the text for a cached state.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return False when no state has been stored.
 */
bool PumpMqttBridge::_formatState(uint8_t moduleSlot, char* out,
                                  size_t outCap) const
{
  PumpState state = PumpState::Off;
  if (!_poller.pumpState(moduleSlot, &state))
  {
    return false;
  }
  const char* text = "fault";
  if (state == PumpState::On)
  {
    text = "on";
  }
  else if (state == PumpState::Off)
  {
    text = "off";
  }
  std::snprintf(out, outCap, "%s", text);
  return true;
}


// ========== Records ==========

namespace
{
/**
 * One published info topic.
 */
enum RecordKind : uint8_t
{
  RecordNow = 0,
  RecordCycle = 1,
  RecordTotals = 2,
  RecordFault = 3
};

/**
 * Names a pump phase for the now payload.
 *
 * @param phase Phase byte.
 * @return off, held, running, or fault. Null when the byte is unknown.
 */
const char* phaseName(uint8_t phase)
{
  if (phase == module_protocol::kPumpPhaseOff)
  {
    return "off";
  }
  if (phase == module_protocol::kPumpPhaseHeld)
  {
    return "held";
  }
  if (phase == module_protocol::kPumpPhaseRunning)
  {
    return "running";
  }
  if (phase == module_protocol::kPumpPhaseFault)
  {
    return "fault";
  }
  return nullptr;
}

/**
 * Names a known fault reason. An unknown reason has no word.
 *
 * @param reason Reason byte.
 * @return The word, or null when the reason is not one of the four.
 */
const char* faultName(uint8_t reason)
{
  if (reason == module_protocol::kFaultNone)
  {
    return "none";
  }
  if (reason == module_protocol::kPumpFaultSlowStart)
  {
    return "slow-start";
  }
  if (reason == module_protocol::kPumpFaultOvercurrent)
  {
    return "overcurrent";
  }
  if (reason == module_protocol::kPumpFaultUndercurrent)
  {
    return "undercurrent";
  }
  if (reason == module_protocol::kPumpFaultStuckOn)
  {
    return "stuck-on";
  }
  return nullptr;
}

/**
 * Topic suffix for one info record.
 *
 * @param record Record index.
 * @return Suffix including the leading slash.
 */
const char* recordSuffix(uint8_t record)
{
  if (record == RecordNow)
  {
    return "/now";
  }
  if (record == RecordCycle)
  {
    return "/cycle";
  }
  if (record == RecordTotals)
  {
    return "/totals";
  }
  return "/fault";
}
}

/**
 * Publishes each stored info sample and fault reason that has not
 * been published yet.
 *
 * @return Nothing.
 */
void PumpMqttBridge::_publishRecords()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t record = 0; record < kRecordCount; ++record)
    {
      char payload[96];
      if (!_recordKnown(slot, record) ||
          !_formatRecord(slot, record, payload, sizeof(payload)))
      {
        _recordRevision[slot][record] = 0;
        if (!_poller.moduleOnline(slot) && _recordOk[slot][record] &&
            !_recordUnavailable[slot][record])
        {
          if (_publishRecord(slot, record, "unavailable"))
          {
            _recordUnavailable[slot][record] = true;
          }
        }
        continue;
      }
      const uint32_t revision = _recordRevisionOf(slot, record);
      if (revision == 0 || revision == _recordRevision[slot][record])
      {
        continue;
      }
      if (!_publishRecord(slot, record, payload))
      {
        continue;
      }
      _recordOk[slot][record] = true;
      _recordUnavailable[slot][record] = false;
      _recordRevision[slot][record] = revision;
    }
  }
}

/**
 * Publishes one info topic with retain off.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param record Index into the now, cycle, totals, and fault set.
 * @param payload Text payload.
 * @return True when publication was accepted.
 */
bool PumpMqttBridge::_publishRecord(uint8_t moduleSlot, uint8_t record,
                                    const char* payload)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/pump%s", _topics.slotPrefix(),
                moduleNumber, recordSuffix(record));
  return _mqttService.publish(topic, payload, false);
}

/**
 * Formats one cached info sample or fault reason.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param record Index into the now, cycle, totals, and fault set.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return False when that sample has not been stored.
 */
bool PumpMqttBridge::_formatRecord(uint8_t moduleSlot, uint8_t record,
                                   char* out, size_t outCap) const
{
  if (record == RecordNow)
  {
    PumpNow sample;
    if (!_poller.pumpNow(moduleSlot, &sample))
    {
      return false;
    }
    const char* phase = phaseName(sample.phase);
    if (phase == nullptr)
    {
      return false;
    }
    const unsigned npgd =
        (sample.flags & module_protocol::kPumpFlagNpgdLow) != 0 ? 1U : 0U;
    const unsigned enable =
        (sample.flags & module_protocol::kPumpFlagEnableHigh) != 0 ? 1U : 0U;
    const unsigned cycled =
        (sample.flags & module_protocol::kPumpFlagCycled) != 0 ? 1U : 0U;
    std::snprintf(out, outCap, "%s %u %u %u %u %u %u", phase, npgd, enable,
                  cycled, static_cast<unsigned>(sample.millivolts),
                  static_cast<unsigned>(sample.milliamps),
                  static_cast<unsigned>(sample.runSeconds));
    return true;
  }
  if (record == RecordCycle)
  {
    PumpCycle sample;
    if (!_poller.pumpCycle(moduleSlot, &sample))
    {
      return false;
    }
    std::snprintf(out, outCap, "%u %u %u %u %u %u %u %u",
                  static_cast<unsigned>(sample.peakMillivolts),
                  static_cast<unsigned>(sample.minimumMillivolts),
                  static_cast<unsigned>(sample.peakMilliamps),
                  static_cast<unsigned>(sample.averageMillivolts),
                  static_cast<unsigned>(sample.averageMilliamps),
                  static_cast<unsigned>(sample.softStartMs),
                  static_cast<unsigned>(sample.powerGoodMs),
                  static_cast<unsigned>(sample.runSeconds));
    return true;
  }
  if (record == RecordTotals)
  {
    PumpTotals sample;
    if (!_poller.pumpTotals(moduleSlot, &sample))
    {
      return false;
    }
    std::snprintf(out, outCap, "%u %u %u %u %u %u",
                  static_cast<unsigned>(sample.completedCycles),
                  static_cast<unsigned>(sample.totalSeconds),
                  static_cast<unsigned>(sample.peakMillivolts),
                  static_cast<unsigned>(sample.peakMilliamps),
                  static_cast<unsigned>(sample.failedStarts),
                  static_cast<unsigned>(sample.lastRunSeconds));
    return true;
  }
  PumpFaultReport report;
  if (!_poller.pumpFaultReport(moduleSlot, &report))
  {
    return false;
  }
  const char* name = faultName(report.reason);
  if (name == nullptr)
  {
    std::snprintf(out, outCap, "%u %u", static_cast<unsigned>(report.reason),
                  static_cast<unsigned>(report.index));
    return true;
  }
  std::snprintf(out, outCap, "%s %u", name,
                static_cast<unsigned>(report.index));
  return true;
}

/**
 * Reports whether one info sample is stored.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param record Index into the now, cycle, totals, and fault set.
 * @return True when a sample can be published.
 */
bool PumpMqttBridge::_recordKnown(uint8_t moduleSlot, uint8_t record) const
{
  if (record == RecordNow)
  {
    return _poller.nowKnown(moduleSlot);
  }
  if (record == RecordCycle)
  {
    return _poller.cycleKnown(moduleSlot);
  }
  if (record == RecordTotals)
  {
    return _poller.totalsKnown(moduleSlot);
  }
  return _poller.faultReportKnown(moduleSlot);
}

/**
 * Reads the generation of one info sample.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param record Index into the now, cycle, totals, and fault set.
 * @return Generation, or 0 when none has been stored.
 */
uint32_t PumpMqttBridge::_recordRevisionOf(uint8_t moduleSlot,
                                           uint8_t record) const
{
  if (record == RecordNow)
  {
    return _poller.nowRevision(moduleSlot);
  }
  if (record == RecordCycle)
  {
    return _poller.cycleRevision(moduleSlot);
  }
  if (record == RecordTotals)
  {
    return _poller.totalsRevision(moduleSlot);
  }
  return _poller.faultRevision(moduleSlot);
}

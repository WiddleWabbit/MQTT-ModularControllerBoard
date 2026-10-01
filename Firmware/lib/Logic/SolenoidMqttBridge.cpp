#include "SolenoidMqttBridge.h"

#include <cstdio>
#include <cstring>

// ========== Construction ==========

SolenoidMqttBridge::SolenoidMqttBridge(SolenoidPoller& poller,
                                       MqttService& mqttService,
                                       MqttTopicLayout& topics)
  : _poller(poller),
    _mqttService(mqttService),
    _topics(topics),
    _seenGeneration(topics.generation())
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
         ++index)
    {
      _publishedOk[slot][index] = false;
      _publishedRevision[slot][index] = 0;
      _published[slot][index][0] = '\0';
    }
    _inventoryPending[slot] = false;
    _inventoryOk[slot] = false;
    _inventoryPayload[slot][0] = '\0';
  }
}


// ========== Public API ==========

/**
 * Records desired states when the topic and payload name one module.
 * Ignores every other message. Safe to call from the MQTT callback.
 *
 * @param topic Received topic.
 * @param payload Payload bytes. Not necessarily NUL-terminated.
 * @param length Payload length.
 * @param context SolenoidMqttBridge instance.
 * @return Nothing.
 */
void SolenoidMqttBridge::onMqttMessage(const char* topic,
                                       const uint8_t* payload, size_t length,
                                       void* context)
{
  SolenoidMqttBridge* bridge = static_cast<SolenoidMqttBridge*>(context);
  if (bridge == nullptr)
  {
    return;
  }
  bridge->_handleMessage(topic, payload, length);
}

/**
 * Publishes each new state, including a repeated value, and one
 * retained unavailable when an output disappears. A rejected publish
 * stays pending. An update with no new state does not publish.
 *
 * @return Nothing.
 */
void SolenoidMqttBridge::update()
{
  _syncTopicGeneration();
  _publishSnapshots();
  _publishInventories();
}


/**
 * Forgets accepted states when the device id has changed.
 *
 * @return Nothing.
 */
void SolenoidMqttBridge::_syncTopicGeneration()
{
  const uint32_t generation = _topics.generation();
  if (generation == _seenGeneration)
  {
    return;
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
         ++index)
    {
      _publishedOk[slot][index] = false;
      _publishedRevision[slot][index] = 0;
      _published[slot][index][0] = '\0';
    }
    _inventoryOk[slot] = false;
    _inventoryPayload[slot][0] = '\0';
  }
  _seenGeneration = generation;
}


// ========== Commands ==========

namespace
{
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
 * Parses one "on" or "off" token.
 *
 * @param cursor Current position. Advanced past the token on success.
 * @param end One past the last payload byte.
 * @param on Set true for on and false for off.
 * @return False when the next token is neither word.
 */
bool readOnOff(const char*& cursor, const char* end, bool* on)
{
  const size_t remaining = static_cast<size_t>(end - cursor);
  if (remaining >= 2 && cursor[0] == 'o' && cursor[1] == 'n' &&
      (remaining == 2 || cursor[2] == ' ' || cursor[2] == '\t'))
  {
    *on = true;
    cursor += 2;
    return true;
  }
  if (remaining >= 3 && cursor[0] == 'o' && cursor[1] == 'f' &&
      cursor[2] == 'f' &&
      (remaining == 3 || cursor[3] == ' ' || cursor[3] == '\t'))
  {
    *on = false;
    cursor += 3;
    return true;
  }
  return false;
}
}

/**
 * Parses a desired-state command and records it.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void SolenoidMqttBridge::_handleMessage(const char* topic,
                                        const uint8_t* payload, size_t length)
{
  if (topic == nullptr)
  {
    return;
  }
  if (std::strcmp(topic, _topics.solenoidConnected()) == 0)
  {
    _handleConnectedQuery(payload, length);
    return;
  }
  if (std::strcmp(topic, _topics.solenoidCommand()) != 0)
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

  bool desiredOn[module_protocol::kMaxSolenoidsPerModule];
  uint8_t count = 0;
  while (cursor < end)
  {
    if (count >= module_protocol::kMaxSolenoidsPerModule)
    {
      return;
    }
    if (!readOnOff(cursor, end, &desiredOn[count]))
    {
      return;
    }
    count = static_cast<uint8_t>(count + 1);
    skipSpace(cursor, end);
  }
  if (count < 1)
  {
    return;
  }
  _poller.commandStates(static_cast<uint8_t>(moduleNumber - 1), desiredOn,
                        count);
}

/**
 * Records a request to publish one slot's connected outputs.
 * Does not record desired state and does not touch the silence window.
 *
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void SolenoidMqttBridge::_handleConnectedQuery(const uint8_t* payload,
                                               size_t length)
{
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
  skipSpace(cursor, end);
  if (cursor != end)
  {
    return;
  }
  _inventoryPending[moduleNumber - 1] = true;
}


// ========== Publication ==========

/**
 * Publishes each stored state that has not been published yet.
 *
 * @return Nothing.
 */
void SolenoidMqttBridge::_publishSnapshots()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const bool active = _poller.countKnown(slot);
    const uint8_t count = _poller.solenoidCount(slot);
    for (uint8_t index = 0; index < module_protocol::kMaxSolenoidsPerModule;
         ++index)
    {
      char payload[16];
      if (!active || index >= count ||
          !_formatState(slot, index, payload, sizeof(payload)))
      {
        if (_publishedOk[slot][index] &&
            std::strcmp(_published[slot][index], "unavailable") != 0)
        {
          if (_publish(slot, index, "unavailable", true))
          {
            std::snprintf(_published[slot][index],
                          sizeof(_published[slot][index]), "unavailable");
          }
        }
        continue;
      }
      const uint32_t revision = _poller.stateRevision(slot, index);
      if (revision == 0 || revision == _publishedRevision[slot][index])
      {
        continue;
      }
      if (!_publish(slot, index, payload, true))
      {
        continue;
      }
      std::snprintf(_published[slot][index], sizeof(_published[slot][index]),
                    "%s", payload);
      _publishedOk[slot][index] = true;
      _publishedRevision[slot][index] = revision;
    }
  }
}

/**
 * Publishes the retained output list for each slot that can answer.
 * A slot that could answer and no longer can publishes unavailable.
 *
 * @return Nothing.
 */
void SolenoidMqttBridge::_publishInventories()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    char text[80];
    if (!_formatInventory(slot, text, sizeof(text)))
    {
      if (_inventoryOk[slot] && _publishInventory(slot, "unavailable"))
      {
        _inventoryOk[slot] = false;
        _inventoryPayload[slot][0] = '\0';
        _inventoryPending[slot] = false;
      }
      continue;
    }
    const bool changed = !_inventoryOk[slot] ||
                         std::strcmp(_inventoryPayload[slot], text) != 0;
    if (!changed && !_inventoryPending[slot])
    {
      continue;
    }
    if (!_publishInventory(slot, text))
    {
      continue;
    }
    std::snprintf(_inventoryPayload[slot], sizeof(_inventoryPayload[slot]), "%s",
                  text);
    _inventoryOk[slot] = true;
    _inventoryPending[slot] = false;
  }
}

/**
 * Formats "<count> <connected indexes...>" for one slot.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return False until the count and every output state are known.
 */
bool SolenoidMqttBridge::_formatInventory(uint8_t moduleSlot, char* out,
                                         size_t outCap) const
{
  if (!_poller.countKnown(moduleSlot) || out == nullptr || outCap < 2)
  {
    return false;
  }
  const uint8_t count = _poller.solenoidCount(moduleSlot);
  if (count > module_protocol::kMaxSolenoidsPerModule)
  {
    return false;
  }
  int used = std::snprintf(out, outCap, "%u", static_cast<unsigned>(count));
  if (used < 0 || static_cast<size_t>(used) >= outCap)
  {
    return false;
  }
  for (uint8_t index = 0; index < count; ++index)
  {
    SolenoidOutputState state = SolenoidOutputState::Off;
    if (!_poller.solenoidState(moduleSlot, index, &state))
    {
      return false;
    }
    if (state == SolenoidOutputState::Disconnected)
    {
      continue;
    }
    const int next = std::snprintf(out + used, outCap - static_cast<size_t>(used),
                                   " %u", static_cast<unsigned>(index) + 1U);
    if (next < 0 || static_cast<size_t>(next) >= outCap - static_cast<size_t>(used))
    {
      return false;
    }
    used += next;
  }
  return true;
}

/**
 * Publishes the retained inventory topic for one slot.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param payload Text payload.
 * @return True when publication was accepted.
 */
bool SolenoidMqttBridge::_publishInventory(uint8_t moduleSlot,
                                          const char* payload)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/solenoids",
                _topics.slotPrefix(), moduleNumber);
  return _mqttService.publish(topic, payload, true);
}

/**
 * Publishes one solenoid topic.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @param payload Text payload.
 * @param retained Retained-message flag.
 * @return True when publication was accepted.
 */
bool SolenoidMqttBridge::_publish(uint8_t moduleSlot, uint8_t solenoidIndex,
                                  const char* payload, bool retained)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  const unsigned solenoidNumber = static_cast<unsigned>(solenoidIndex) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/solenoid/%u",
                _topics.slotPrefix(), moduleNumber, solenoidNumber);
  return _mqttService.publish(topic, payload, retained);
}

/**
 * Formats the retained text for a cached state.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param solenoidIndex Zero-based output.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return False when no state has been stored.
 */
bool SolenoidMqttBridge::_formatState(uint8_t moduleSlot,
                                      uint8_t solenoidIndex, char* out,
                                      size_t outCap) const
{
  SolenoidOutputState state = SolenoidOutputState::Off;
  if (!_poller.solenoidState(moduleSlot, solenoidIndex, &state))
  {
    return false;
  }
  const char* text = "disconnected";
  if (state == SolenoidOutputState::On)
  {
    text = "on";
  }
  else if (state == SolenoidOutputState::Off)
  {
    text = "off";
  }
  std::snprintf(out, outCap, "%s", text);
  return true;
}

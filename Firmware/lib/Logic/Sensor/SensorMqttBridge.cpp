#include "SensorMqttBridge.h"

#include <cstdio>
#include <cstring>

// ========== Construction ==========

SensorMqttBridge::SensorMqttBridge(SensorPoller& poller,
                                   MqttService& mqttService,
                                   MqttTopicLayout& topics)
  : _poller(poller),
    _mqttService(mqttService),
    _topics(topics),
    _seenGeneration(topics.generation()),
    _demandHeld(false)
{
  _heldDemand.moduleSlot = 0;
  _heldDemand.sensorIndex = 0;
  _heldDemand.ok = false;
  _heldDemand.connected = false;
  _heldDemand.value = 0;
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
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
 * Enqueues a reading, or records a connected-list request, when the
 * topic and payload name one sensor slot. Ignores every other
 * message. Safe to call from the MQTT callback.
 *
 * @param topic Received topic.
 * @param payload Payload bytes. Not necessarily NUL-terminated.
 * @param length Payload length.
 * @param context SensorMqttBridge instance.
 * @return Nothing.
 */
void SensorMqttBridge::onMqttMessage(const char* topic,
                                     const uint8_t* payload, size_t length,
                                     void* context)
{
  SensorMqttBridge* bridge = static_cast<SensorMqttBridge*>(context);
  if (bridge == nullptr)
  {
    return;
  }
  bridge->_handleMessage(topic, payload, length);
}

/**
 * Publishes each new reading, including a repeated value, and one
 * retained unavailable when an input disappears. Publishes the
 * connected-input list when that text changes or a request asked
 * again. A rejected publish stays pending. An update with no new
 * reading and no list change does not publish.
 *
 * @return Nothing.
 */
void SensorMqttBridge::update()
{
  _syncTopicGeneration();
  _publishDemand();
  _publishSnapshots();
  _publishInventories();
}


/**
 * Forgets accepted readings and lists when the device id has changed.
 *
 * @return Nothing.
 */
void SensorMqttBridge::_syncTopicGeneration()
{
  const uint32_t generation = _topics.generation();
  if (generation == _seenGeneration)
  {
    return;
  }
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
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
    if (parsed > module_protocol::kMaxSensorsPerModule)
    {
      return false;
    }
    ++cursor;
  }
  *value = parsed;
  return true;
}
}

/**
 * Parses a read command and enqueues it, or records a connected-list
 * request.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void SensorMqttBridge::_handleMessage(const char* topic,
                                      const uint8_t* payload, size_t length)
{
  if (topic == nullptr)
  {
    return;
  }
  if (std::strcmp(topic, _topics.sensorConnected()) == 0)
  {
    _handleConnectedQuery(payload, length);
    return;
  }
  if (std::strcmp(topic, _topics.sensorRead()) != 0)
  {
    return;
  }
  if (payload == nullptr && length > 0)
  {
    return;
  }
  const char* cursor = reinterpret_cast<const char*>(payload);
  const char* end = cursor + length;
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
  unsigned moduleNumber = 0;
  if (!readUnsigned(cursor, end, &moduleNumber))
  {
    return;
  }
  if (cursor >= end || (*cursor != ' ' && *cursor != '\t'))
  {
    return;
  }
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
  unsigned sensorNumber = 0;
  if (!readUnsigned(cursor, end, &sensorNumber))
  {
    return;
  }
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
  if (cursor != end)
  {
    return;
  }
  if (moduleNumber < 1 || moduleNumber > module_protocol::kSlotCount ||
      sensorNumber < 1 ||
      sensorNumber > module_protocol::kMaxSensorsPerModule)
  {
    return;
  }
  _poller.requestReading(static_cast<uint8_t>(moduleNumber - 1),
                         static_cast<uint8_t>(sensorNumber - 1));
}

/**
 * Records a request to publish one slot's connected inputs.
 * Does not enqueue a reading.
 *
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void SensorMqttBridge::_handleConnectedQuery(const uint8_t* payload,
                                             size_t length)
{
  if (payload == nullptr && length > 0)
  {
    return;
  }
  const char* cursor = reinterpret_cast<const char*>(payload);
  const char* end = cursor + length;
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
  unsigned moduleNumber = 0;
  if (!readUnsigned(cursor, end, &moduleNumber))
  {
    return;
  }
  if (moduleNumber < 1 || moduleNumber > module_protocol::kSlotCount)
  {
    return;
  }
  while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
  {
    ++cursor;
  }
  if (cursor != end)
  {
    return;
  }
  _inventoryPending[moduleNumber - 1] = true;
}


// ========== Publication ==========

/**
 * Publishes the held on-demand result when one is waiting.
 *
 * @return Nothing.
 */
void SensorMqttBridge::_publishDemand()
{
  if (!_demandHeld)
  {
    _demandHeld = _poller.takeDemandResult(&_heldDemand);
  }
  if (!_demandHeld)
  {
    return;
  }

  char payload[32];
  bool retained = false;
  if (_heldDemand.ok)
  {
    _formatReading(_heldDemand.connected, _heldDemand.value, payload,
                   sizeof(payload));
    retained = true;
  }
  else
  {
    std::snprintf(payload, sizeof(payload), "unavailable");
  }
  if (!_publish(_heldDemand.moduleSlot, _heldDemand.sensorIndex, payload,
                retained))
  {
    return;
  }
  if (retained)
  {
    std::snprintf(_published[_heldDemand.moduleSlot][_heldDemand.sensorIndex],
                  sizeof(_published[_heldDemand.moduleSlot]
                                   [_heldDemand.sensorIndex]),
                  "%s", payload);
    _publishedOk[_heldDemand.moduleSlot][_heldDemand.sensorIndex] = true;
    _publishedRevision[_heldDemand.moduleSlot][_heldDemand.sensorIndex] =
        _poller.sampleRevision(_heldDemand.moduleSlot,
                               _heldDemand.sensorIndex);
  }
  _demandHeld = false;
}

/**
 * Publishes each stored reading that has not been published yet.
 *
 * @return Nothing.
 */
void SensorMqttBridge::_publishSnapshots()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    const bool active = _poller.countKnown(slot);
    const uint8_t count = _poller.sensorCount(slot);
    for (uint8_t index = 0; index < module_protocol::kMaxSensorsPerModule;
         ++index)
    {
      char payload[32];
      if (!active || index >= count ||
          !_formatSample(slot, index, payload, sizeof(payload)))
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
      const uint32_t revision = _poller.sampleRevision(slot, index);
      if (revision == 0 || revision == _publishedRevision[slot][index])
      {
        continue;
      }
      if (_demandHeld && _heldDemand.moduleSlot == slot &&
          _heldDemand.sensorIndex == index)
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
 * Publishes the retained input list for each slot that can answer.
 * A slot that could answer and no longer can publishes unavailable.
 *
 * @return Nothing.
 */
void SensorMqttBridge::_publishInventories()
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
    std::snprintf(_inventoryPayload[slot], sizeof(_inventoryPayload[slot]),
                  "%s", text);
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
 * @return False until the count and every input's presence are known.
 */
bool SensorMqttBridge::_formatInventory(uint8_t moduleSlot, char* out,
                                       size_t outCap) const
{
  if (!_poller.countKnown(moduleSlot) || out == nullptr || outCap < 2)
  {
    return false;
  }
  const uint8_t count = _poller.sensorCount(moduleSlot);
  if (count > module_protocol::kMaxSensorsPerModule)
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
    bool connected = false;
    bool hasValue = false;
    int32_t value = 0;
    if (!_poller.sensorSample(moduleSlot, index, &connected, &hasValue,
                              &value))
    {
      return false;
    }
    if (!connected)
    {
      continue;
    }
    const int next = std::snprintf(out + used,
                                   outCap - static_cast<size_t>(used),
                                   " %u", static_cast<unsigned>(index) + 1U);
    if (next < 0 ||
        static_cast<size_t>(next) >= outCap - static_cast<size_t>(used))
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
bool SensorMqttBridge::_publishInventory(uint8_t moduleSlot,
                                        const char* payload)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/sensors",
                _topics.slotPrefix(), moduleNumber);
  return _mqttService.publish(topic, payload, true);
}

/**
 * Publishes one sensor topic.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param payload Text payload.
 * @param retained Retained-message flag.
 * @return True when publication was accepted.
 */
bool SensorMqttBridge::_publish(uint8_t moduleSlot, uint8_t sensorIndex,
                                const char* payload, bool retained)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  const unsigned sensorNumber = static_cast<unsigned>(sensorIndex) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/sensor/%u",
                _topics.slotPrefix(), moduleNumber, sensorNumber);
  return _mqttService.publish(topic, payload, retained);
}

/**
 * Formats the retained text for a cached sample.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param sensorIndex Zero-based input.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return False when presence is not known yet, or when the sensor
 *         is connected but the reading has not arrived.
 */
bool SensorMqttBridge::_formatSample(uint8_t moduleSlot, uint8_t sensorIndex,
                                     char* out, size_t outCap) const
{
  bool connected = false;
  bool hasValue = false;
  int32_t value = 0;
  if (!_poller.sensorSample(moduleSlot, sensorIndex, &connected, &hasValue,
                            &value))
  {
    return false;
  }
  if (connected && !hasValue)
  {
    return false;
  }
  _formatReading(connected, value, out, outCap);
  return true;
}

/**
 * Formats a successful reading payload.
 *
 * @param connected Presence flag.
 * @param value Raw value, used only when connected is true.
 * @param out Destination buffer.
 * @param outCap Destination capacity.
 * @return Nothing.
 */
void SensorMqttBridge::_formatReading(bool connected, int32_t value, char* out,
                                      size_t outCap)
{
  if (!connected)
  {
    std::snprintf(out, outCap, "disconnected");
    return;
  }
  std::snprintf(out, outCap, "connected %ld", static_cast<long>(value));
}

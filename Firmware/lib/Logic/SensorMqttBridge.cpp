#include "SensorMqttBridge.h"

#include <cstdio>
#include <cstring>

// ========== Construction ==========

SensorMqttBridge::SensorMqttBridge(SensorPoller& poller,
                                   MqttService& mqttService,
                                   const char* slotTopicPrefix)
  : _poller(poller),
    _mqttService(mqttService),
    _slotTopicPrefix(slotTopicPrefix == nullptr ? "" : slotTopicPrefix),
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
  }
}


// ========== Public API ==========

/**
 * Enqueues a reading when topic and payload name one sensor.
 * Ignores every other message. Safe to call from the MQTT callback.
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
 * retained unavailable when an input disappears. A rejected publish
 * stays pending. An update with no new reading does not publish.
 *
 * @return Nothing.
 */
void SensorMqttBridge::update()
{
  _publishDemand();
  _publishSnapshots();
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
 * Parses a read command and enqueues it.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void SensorMqttBridge::_handleMessage(const char* topic,
                                      const uint8_t* payload, size_t length)
{
  if (topic == nullptr || std::strcmp(topic, kSensorReadTopic) != 0)
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
                _slotTopicPrefix.c_str(), moduleNumber, sensorNumber);
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

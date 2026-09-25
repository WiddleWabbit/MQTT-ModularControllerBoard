#include "PumpMqttBridge.h"

#include <cstdio>
#include <cstring>

// ========== Construction ==========

PumpMqttBridge::PumpMqttBridge(PumpPoller& poller, MqttService& mqttService,
                               const char* slotTopicPrefix)
  : _poller(poller),
    _mqttService(mqttService),
    _slotTopicPrefix(slotTopicPrefix == nullptr ? "" : slotTopicPrefix)
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _publishedOk[slot] = false;
    _publishedRevision[slot] = 0;
    _published[slot][0] = '\0';
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
 * Publishes each new state, including a repeated value, and one
 * retained unavailable when the pump disappears. A rejected publish
 * stays pending. An update with no new state does not publish.
 *
 * @return Nothing.
 */
void PumpMqttBridge::update()
{
  _publishSnapshots();
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
  if (topic == nullptr || std::strcmp(topic, kPumpCommandTopic) != 0)
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
        if (_publish(slot, "unavailable", true))
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
    if (!_publish(slot, payload, true))
    {
      continue;
    }
    std::snprintf(_published[slot], sizeof(_published[slot]), "%s", payload);
    _publishedOk[slot] = true;
    _publishedRevision[slot] = revision;
  }
}

/**
 * Publishes one pump topic.
 *
 * @param moduleSlot Firmware slot 0..3.
 * @param payload Text payload.
 * @param retained Retained-message flag.
 * @return True when publication was accepted.
 */
bool PumpMqttBridge::_publish(uint8_t moduleSlot, const char* payload,
                              bool retained)
{
  char topic[96];
  const unsigned moduleNumber = static_cast<unsigned>(moduleSlot) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u/pump", _slotTopicPrefix.c_str(),
                moduleNumber);
  return _mqttService.publish(topic, payload, retained);
}

/**
 * Formats the retained text for a cached state.
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

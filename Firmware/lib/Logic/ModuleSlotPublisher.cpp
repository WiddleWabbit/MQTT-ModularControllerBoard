#include "ModuleSlotPublisher.h"

#include <cstdio>
#include <cstring>

#include "SlotStatusText.h"

// ========== Construction ==========

ModuleSlotPublisher::ModuleSlotPublisher(ModuleHost& moduleHost,
                                         MqttService& mqttService,
                                         const char* topicPrefix)
  : _moduleHost(moduleHost),
    _mqttService(mqttService),
    _topicPrefix(topicPrefix == nullptr ? "" : topicPrefix)
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _published[slot][0] = '\0';
    _publishedOk[slot] = false;
  }
}


// ========== Public API ==========

/**
 * Publishes each slot whose public status text differs from the last
 * accepted payload. A rejected publish stays pending.
 *
 * @return Nothing.
 */
void ModuleSlotPublisher::update()
{
  for (uint8_t slot = 0; slot < module_protocol::kSlotCount; ++slot)
  {
    _publishIfChanged(slot);
  }
}


// ========== Publication ==========

/**
 * Publishes one slot when its snapshot text is not the last success.
 *
 * @param slotIndex Firmware slot 0..3.
 * @return Nothing.
 */
void ModuleSlotPublisher::_publishIfChanged(uint8_t slotIndex)
{
  char payload[kSlotStatusBodyBytes];
  writeSlotStatusBody(_moduleHost, slotIndex, payload, sizeof(payload));
  if (_publishedOk[slotIndex] &&
      std::strcmp(_published[slotIndex], payload) == 0)
  {
    return;
  }

  char topic[96];
  const unsigned slotNumber = static_cast<unsigned>(slotIndex) + 1U;
  std::snprintf(topic, sizeof(topic), "%s/%u", _topicPrefix.c_str(),
                slotNumber);
  if (!_mqttService.publish(topic, payload, true))
  {
    return;
  }

  std::snprintf(_published[slotIndex], kSlotStatusBodyBytes, "%s", payload);
  _publishedOk[slotIndex] = true;
}

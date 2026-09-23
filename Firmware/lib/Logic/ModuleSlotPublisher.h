#pragma once

#include <cstdint>
#include <string>

#include "ModuleHost.h"
#include "ModuleProtocol.h"
#include "MqttService.h"
#include "SlotStatusText.h"

/**
 * Publishes retained MQTT snapshots of each slot's public status.
 * Reads ModuleHost only. Does not call update(), ping(), or echo(),
 * and ModuleHost does not depend on the MQTT client.
 */
class ModuleSlotPublisher
{
public:
  /**
   * Creates a publisher for the four firmware slots.
   *
   * @param moduleHost Slot snapshots to read.
   * @param mqttService Connected-only publication path.
   * @param topicPrefix Topic stem. Slot 1 is published at
   *        "{topicPrefix}/1". A null prefix is treated as empty.
   */
  ModuleSlotPublisher(ModuleHost& moduleHost, MqttService& mqttService,
                      const char* topicPrefix);

  /**
   * Publishes each slot whose public status text differs from the last
   * accepted payload. A rejected publish stays pending. Does nothing
   * when every accepted snapshot still matches.
   *
   * @return Nothing.
   */
  void update();

private:
  ModuleHost& _moduleHost;
  MqttService& _mqttService;
  std::string _topicPrefix;
  char _published[module_protocol::kSlotCount][kSlotStatusBodyBytes];
  bool _publishedOk[module_protocol::kSlotCount];

  /**
   * Publishes one slot when its snapshot text is not the last success.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Nothing.
   */
  void _publishIfChanged(uint8_t slotIndex);
};

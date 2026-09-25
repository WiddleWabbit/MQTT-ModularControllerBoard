#pragma once

#include <cstdint>
#include <string>

#include "MqttService.h"
#include "PumpPoller.h"

/**
 * MQTT command topic for one pump.
 * Payload is "<moduleSlot> on", "<moduleSlot> off", or
 * "<moduleSlot> reset". Module slot 1 is firmware index 0.
 * on and off record the desired state. reset asks for a pump reset
 * and does not change the desired state.
 */
static const char kPumpCommandTopic[] = "watering/pump";

/**
 * Publishes pump state and turns the command topic into a poller
 * request. The command handler only records the request; it does
 * not touch I2C. State is published at "{prefix}/{moduleSlot}/pump".
 */
class PumpMqttBridge
{
public:
  /**
   * Creates a bridge for pump commands and state publication.
   *
   * @param poller Scheduler and state cache.
   * @param mqttService Connected-only publication path.
   * @param slotTopicPrefix Topic stem shared with slot status.
   *        The pump on module slot 1 is "{prefix}/1/pump".
   *        A null prefix is treated as empty.
   */
  PumpMqttBridge(PumpPoller& poller, MqttService& mqttService,
                 const char* slotTopicPrefix);

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
  static void onMqttMessage(const char* topic, const uint8_t* payload,
                            size_t length, void* context);

  /**
   * Publishes each new state, including a repeated value, and one
   * retained unavailable when the pump disappears. A rejected publish
   * stays pending. An update with no new state does not publish.
   *
   * @return Nothing.
   */
  void update();

private:
  PumpPoller& _poller;
  MqttService& _mqttService;
  std::string _slotTopicPrefix;
  bool _publishedOk[module_protocol::kSlotCount];
  uint32_t _publishedRevision[module_protocol::kSlotCount];
  char _published[module_protocol::kSlotCount][16];

  /**
   * Parses a pump command and records it.
   *
   * @param topic Received topic.
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleMessage(const char* topic, const uint8_t* payload,
                      size_t length);

  /**
   * Publishes each stored state that has not been published yet.
   *
   * @return Nothing.
   */
  void _publishSnapshots();

  /**
   * Publishes one pump topic.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param payload Text payload.
   * @param retained Retained-message flag.
   * @return True when publication was accepted.
   */
  bool _publish(uint8_t moduleSlot, const char* payload, bool retained);

  /**
   * Formats the retained text for a cached state.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False when no state has been stored.
   */
  bool _formatState(uint8_t moduleSlot, char* out, size_t outCap) const;
};

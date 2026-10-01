#pragma once

#include <cstdint>

#include "MqttService.h"
#include "MqttTopicLayout.h"
#include "SolenoidPoller.h"

/**
 * Solenoid desired-state topic when the device id is watering.
 * Payload is "<moduleSlot> <on|off> ...", module slot 1-based.
 * One state word per output, in solenoid order. Module slot 1 is
 * firmware index 0. Solenoid 1 is wire index 0.
 */
static const char kSolenoidCommandTopic[] = "watering/solenoids";

/**
 * Solenoid connected-output topic when the device id is watering.
 * Payload is the 1-based module slot, for example "1".
 * This is not a desired-state command and does not restart the
 * 15-minute silence window.
 */
static const char kSolenoidConnectedTopic[] = "watering/solenoids/connected";

/**
 * Publishes solenoid states and turns the command topic into a poller
 * request. The command handler only records desired states; it does
 * not touch I2C. States are published at
 * "{prefix}/{moduleSlot}/solenoid/{solenoid}".
 */
class SolenoidMqttBridge
{
public:
  /**
   * Creates a bridge for solenoid commands and state publication.
   *
   * @param poller Scheduler and state cache.
   * @param mqttService Connected-only publication path.
   * @param topics Topic tree. Solenoid 1 on module slot 1 is
   *        "{id}/slot/1/solenoid/1".
   */
  SolenoidMqttBridge(SolenoidPoller& poller, MqttService& mqttService,
                     MqttTopicLayout& topics);

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
  static void onMqttMessage(const char* topic, const uint8_t* payload,
                            size_t length, void* context);

  /**
   * Publishes each new state, including a repeated value, and one
   * retained unavailable when an output disappears. A rejected publish
   * stays pending. An update with no new state does not publish.
   * A new device id sends the current states once on the new topics.
   *
   * @return Nothing.
   */
  void update();

private:
  SolenoidPoller& _poller;
  MqttService& _mqttService;
  MqttTopicLayout& _topics;
  uint32_t _seenGeneration;
  bool _publishedOk[module_protocol::kSlotCount]
                   [module_protocol::kMaxSolenoidsPerModule];
  uint32_t _publishedRevision[module_protocol::kSlotCount]
                             [module_protocol::kMaxSolenoidsPerModule];
  char _published[module_protocol::kSlotCount]
                 [module_protocol::kMaxSolenoidsPerModule][16];
  bool _inventoryPending[module_protocol::kSlotCount];
  bool _inventoryOk[module_protocol::kSlotCount];
  char _inventoryPayload[module_protocol::kSlotCount][80];

  /**
   * Parses a desired-state command and records it.
   *
   * @param topic Received topic.
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleMessage(const char* topic, const uint8_t* payload,
                      size_t length);

  /**
   * Forgets accepted states when the device id has changed.
   *
   * @return Nothing.
   */
  void _syncTopicGeneration();

  /**
   * Records a request to publish one slot's connected outputs.
   * Does not record desired state and does not touch the silence window.
   *
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleConnectedQuery(const uint8_t* payload, size_t length);

  /**
   * Publishes each stored state that has not been published yet.
   *
   * @return Nothing.
   */
  void _publishSnapshots();

  /**
   * Publishes the retained output list for each slot that can answer.
   * A slot that could answer and no longer can publishes unavailable.
   *
   * @return Nothing.
   */
  void _publishInventories();

  /**
   * Formats "<count> <connected indexes...>" for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False until the count and every output state are known.
   */
  bool _formatInventory(uint8_t moduleSlot, char* out, size_t outCap) const;

  /**
   * Publishes the retained inventory topic for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publishInventory(uint8_t moduleSlot, const char* payload);

  /**
   * Publishes one solenoid topic.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @param payload Text payload.
   * @param retained Retained-message flag.
   * @return True when publication was accepted.
   */
  bool _publish(uint8_t moduleSlot, uint8_t solenoidIndex, const char* payload,
                bool retained);

  /**
   * Formats the retained text for a cached state.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False when no state has been stored.
   */
  bool _formatState(uint8_t moduleSlot, uint8_t solenoidIndex, char* out,
                    size_t outCap) const;
};

#pragma once

#include <cstdint>
#include <string>

#include "MqttService.h"
#include "SolenoidPoller.h"

/**
 * MQTT command topic for solenoid desired states.
 * Payload is "<moduleSlot> <on|off> ...", module slot 1-based.
 * One state word per output, in solenoid order. Module slot 1 is
 * firmware index 0. Solenoid 1 is wire index 0.
 */
static const char kSolenoidCommandTopic[] = "watering/solenoids";

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
   * @param slotTopicPrefix Topic stem shared with slot status.
   *        Solenoid 1 on module slot 1 is "{prefix}/1/solenoid/1".
   *        A null prefix is treated as empty.
   */
  SolenoidMqttBridge(SolenoidPoller& poller, MqttService& mqttService,
                     const char* slotTopicPrefix);

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
   *
   * @return Nothing.
   */
  void update();

private:
  SolenoidPoller& _poller;
  MqttService& _mqttService;
  std::string _slotTopicPrefix;
  bool _publishedOk[module_protocol::kSlotCount]
                   [module_protocol::kMaxSolenoidsPerModule];
  uint32_t _publishedRevision[module_protocol::kSlotCount]
                             [module_protocol::kMaxSolenoidsPerModule];
  char _published[module_protocol::kSlotCount]
                 [module_protocol::kMaxSolenoidsPerModule][16];

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
   * Publishes each stored state that has not been published yet.
   *
   * @return Nothing.
   */
  void _publishSnapshots();

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

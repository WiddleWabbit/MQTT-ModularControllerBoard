#pragma once

#include <cstdint>

#include "MqttService.h"
#include "MqttTopicLayout.h"
#include "PumpPoller.h"

/**
 * Pump command topic when the device id is watering.
 * Payload is "<moduleSlot> on", "<moduleSlot> off", or
 * "<moduleSlot> reset". Module slot 1 is firmware index 0.
 * on and off record the desired state. reset asks for a pump reset
 * and does not change the desired state.
 */
static const char kPumpCommandTopic[] = "watering/pump";

/**
 * Publishes pump state and the info samples, and turns the command
 * topic into a poller request. The command handler only records the
 * request; it does not touch I2C. State is published at
 * "{prefix}/{moduleSlot}/pump". Now, cycle, totals, and the fault
 * reason use the same stem with /now, /cycle, /totals, and /fault.
 */
class PumpMqttBridge
{
public:
  /**
   * Creates a bridge for pump commands and state publication.
   *
   * @param poller Scheduler and state cache.
   * @param mqttService Connected-only publication path.
   * @param topics Topic tree. The pump on module slot 1 is
   *        "{id}/slot/1/pump".
   */
  PumpMqttBridge(PumpPoller& poller, MqttService& mqttService,
                 MqttTopicLayout& topics);

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
   * Publishes each new state and info sample, including a repeated
   * value, with retain off. A pump that disappears publishes
   * unavailable on the state topic only. Now, cycle, totals, and
   * the fault reason are left as last published. A rejected publish
   * stays pending. A new device id sends the current values once
   * on the new topics.
   *
   * @return Nothing.
   */
  void update();

private:
  static const uint8_t kRecordCount = 4;

  PumpPoller& _poller;
  MqttService& _mqttService;
  MqttTopicLayout& _topics;
  uint32_t _seenGeneration;
  bool _publishedOk[module_protocol::kSlotCount];
  uint32_t _publishedRevision[module_protocol::kSlotCount];
  char _published[module_protocol::kSlotCount][16];
  uint32_t _recordRevision[module_protocol::kSlotCount][kRecordCount];

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
   * Forgets accepted states when the device id has changed.
   *
   * @return Nothing.
   */
  void _syncTopicGeneration();

  /**
   * Publishes each stored state that has not been published yet.
   *
   * @return Nothing.
   */
  void _publishSnapshots();

  /**
   * Publishes each stored info sample and fault reason that has not
   * been published yet.
   *
   * @return Nothing.
   */
  void _publishRecords();

  /**
   * Publishes one pump topic with retain off.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publish(uint8_t moduleSlot, const char* payload);

  /**
   * Publishes one info topic with retain off.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param record Index into the now, cycle, totals, and fault set.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publishRecord(uint8_t moduleSlot, uint8_t record,
                      const char* payload);

  /**
   * Formats the text for a cached state.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False when no state has been stored.
   */
  bool _formatState(uint8_t moduleSlot, char* out, size_t outCap) const;

  /**
   * Formats one cached info sample or fault reason.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param record Index into the now, cycle, totals, and fault set.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False when that sample has not been stored.
   */
  bool _formatRecord(uint8_t moduleSlot, uint8_t record, char* out,
                     size_t outCap) const;

  /**
   * Reports whether one info sample is stored.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param record Index into the now, cycle, totals, and fault set.
   * @return True when a sample can be published.
   */
  bool _recordKnown(uint8_t moduleSlot, uint8_t record) const;

  /**
   * Reads the generation of one info sample.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param record Index into the now, cycle, totals, and fault set.
   * @return Generation, or 0 when none has been stored.
   */
  uint32_t _recordRevisionOf(uint8_t moduleSlot, uint8_t record) const;
};

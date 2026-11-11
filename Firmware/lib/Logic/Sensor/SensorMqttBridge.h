#pragma once

#include <cstdint>

#include "MqttService.h"
#include "MqttTopicLayout.h"
#include "SensorPoller.h"

/**
 * Sensor on/off/reset topic when the device id is watering.
 * Payload is "<moduleSlot> <sensor> on", "off", or "reset".
 * Module slot 1 is firmware index 0. Sensor 1 is wire index 0.
 * on and off record the desired state. reset asks for a reset and
 * does not change the desired state.
 */
static const char kSensorCommandTopic[] = "watering/sensor";

/**
 * Sensor read topic when the device id is watering.
 * Payload is "<moduleSlot> <sensor>", both 1-based. Module slot 1 is
 * firmware index 0. Sensor 1 is wire index 0.
 */
static const char kSensorReadTopic[] = "watering/sensor/read";

/**
 * Sensor connected-input topic when the device id is watering.
 * Payload is the 1-based module slot, for example "1".
 * This is not a reading request.
 */
static const char kSensorConnectedTopic[] = "watering/sensor/connected";

/**
 * Publishes sensor samples and turns the read command into a poller
 * request. The command handler only records a request; it does not
 * touch I2C. Readings are published at
 * "{prefix}/{moduleSlot}/sensor/{sensor}". The connected list is
 * published at "{prefix}/{moduleSlot}/sensors". The fault list is
 * published at "{prefix}/{moduleSlot}/sensors/fault".
 */
class SensorMqttBridge
{
public:
  /**
   * Creates a bridge for sensor commands and reading publication.
   *
   * @param poller Scheduler and sample cache.
   * @param mqttService Connected-only publication path.
   * @param topics Topic tree. Sensor 1 on module slot 1 is
   *        "{id}/slot/1/sensor/1".
   */
  SensorMqttBridge(SensorPoller& poller, MqttService& mqttService,
                   MqttTopicLayout& topics);

  /**
   * Records an on, off, or reset, enqueues a reading, or records a
   * connected-list request. Ignores every other message. Safe to
   * call from the MQTT callback.
   *
   * @param topic Received topic.
   * @param payload Payload bytes. Not necessarily NUL-terminated.
   * @param length Payload length.
   * @param context SensorMqttBridge instance.
   * @return Nothing.
   */
  static void onMqttMessage(const char* topic, const uint8_t* payload,
                            size_t length, void* context);

  /**
   * Publishes each new reading, including a repeated value, with
   * retain off. An input that disappears publishes unavailable with
   * retain off. Publishes the connected-input list when that text
   * changes or a request asked again. Publishes the fault list when
   * a reason is stored or a fault clears. A rejected publish stays
   * pending. A new device id sends the current readings and the
   * lists once on the new topics.
   *
   * @return Nothing.
   */
  void update();

private:
  SensorPoller& _poller;
  MqttService& _mqttService;
  MqttTopicLayout& _topics;
  uint32_t _seenGeneration;
  bool _demandHeld;
  SensorDemandResult _heldDemand;
  bool _publishedOk[module_protocol::kSlotCount]
                   [module_protocol::kMaxSensorsPerModule];
  uint32_t _publishedRevision[module_protocol::kSlotCount]
                             [module_protocol::kMaxSensorsPerModule];
  char _published[module_protocol::kSlotCount]
                 [module_protocol::kMaxSensorsPerModule][32];
  bool _inventoryPending[module_protocol::kSlotCount];
  bool _inventoryOk[module_protocol::kSlotCount];
  char _inventoryPayload[module_protocol::kSlotCount][80];
  bool _faultOk[module_protocol::kSlotCount];
  uint32_t _faultRevision[module_protocol::kSlotCount];

  /**
   * Parses an on/off/reset, a read, or a connected-list request.
   *
   * @param topic Received topic.
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleMessage(const char* topic, const uint8_t* payload,
                      size_t length);

  /**
   * Forgets accepted readings and lists when the device id has changed.
   *
   * @return Nothing.
   */
  void _syncTopicGeneration();

  /**
   * Records a request to publish one slot's connected inputs.
   * Does not enqueue a reading.
   *
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleConnectedQuery(const uint8_t* payload, size_t length);

  /**
   * Parses an on, off, or reset for one input and records it.
   *
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleCommand(const uint8_t* payload, size_t length);

  /**
   * Publishes the held on-demand result when one is waiting.
   *
   * @return Nothing.
   */
  void _publishDemand();

  /**
   * Publishes each stored reading that has not been published yet.
   *
   * @return Nothing.
   */
  void _publishSnapshots();

  /**
   * Publishes the input list for each slot that can answer.
   * A slot that could answer and no longer can publishes unavailable.
   * Neither payload is retained.
   *
   * @return Nothing.
   */
  void _publishInventories();

  /**
   * Publishes the fault list for each slot that has one stored.
   * A slot that could answer and no longer can publishes unavailable.
   * Neither payload is retained.
   *
   * @return Nothing.
   */
  void _publishFaultLists();

  /**
   * Formats "<count> <connected indexes...>" for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False until the count and every input's presence are known.
   */
  bool _formatInventory(uint8_t moduleSlot, char* out, size_t outCap) const;

  /**
   * Publishes the inventory topic for one slot with retain off.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publishInventory(uint8_t moduleSlot, const char* payload);

  /**
   * Publishes the fault-list topic for one slot with retain off.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publishFault(uint8_t moduleSlot, const char* payload);

  /**
   * Formats "<count> <reason>..." for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False until the fault list is ready to publish.
   */
  bool _formatFaultList(uint8_t moduleSlot, char* out, size_t outCap) const;

  /**
   * Publishes one sensor topic with retain off.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param payload Text payload.
   * @return True when publication was accepted.
   */
  bool _publish(uint8_t moduleSlot, uint8_t sensorIndex, const char* payload);

  /**
   * Formats the text for a cached sample.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return False when presence is not known yet, or when the sensor
   *         is connected but the reading has not arrived.
   */
  bool _formatSample(uint8_t moduleSlot, uint8_t sensorIndex, char* out,
                     size_t outCap) const;

  /**
   * Formats the text for one stored state.
   *
   * @param state Stored state.
   * @param hasValue True when value is a stored reading.
   * @param value Raw value, used only when state is on.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Nothing.
   */
  static void _formatState(SensorState state, bool hasValue, int32_t value,
                           char* out, size_t outCap);
};

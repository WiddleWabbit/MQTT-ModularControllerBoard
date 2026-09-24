#pragma once

#include <cstdint>
#include <string>

#include "MqttService.h"
#include "SensorPoller.h"

/**
 * MQTT command topic for an immediate sensor reading.
 * Payload is "<moduleSlot> <sensor>", both 1-based. Module slot 1 is
 * firmware index 0. Sensor 1 is wire index 0.
 */
static const char kSensorReadTopic[] = "watering/sensor/read";

/**
 * Publishes sensor samples and turns the read command into a poller
 * request. The command handler only enqueues; it does not touch I2C.
 * Readings are published at "{prefix}/{moduleSlot}/sensor/{sensor}".
 */
class SensorMqttBridge
{
public:
  /**
   * Creates a bridge for sensor commands and reading publication.
   *
   * @param poller Scheduler and sample cache.
   * @param mqttService Connected-only publication path.
   * @param slotTopicPrefix Topic stem shared with slot status.
   *        Sensor 1 on module slot 1 is "{prefix}/1/sensor/1".
   *        A null prefix is treated as empty.
   */
  SensorMqttBridge(SensorPoller& poller, MqttService& mqttService,
                   const char* slotTopicPrefix);

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
  static void onMqttMessage(const char* topic, const uint8_t* payload,
                            size_t length, void* context);

  /**
   * Publishes each new reading, including a repeated value, and one
   * retained unavailable when an input disappears. A rejected publish
   * stays pending. An update with no new reading does not publish.
   *
   * @return Nothing.
   */
  void update();

private:
  SensorPoller& _poller;
  MqttService& _mqttService;
  std::string _slotTopicPrefix;
  bool _demandHeld;
  SensorDemandResult _heldDemand;
  bool _publishedOk[module_protocol::kSlotCount]
                   [module_protocol::kMaxSensorsPerModule];
  uint32_t _publishedRevision[module_protocol::kSlotCount]
                             [module_protocol::kMaxSensorsPerModule];
  char _published[module_protocol::kSlotCount]
                 [module_protocol::kMaxSensorsPerModule][32];

  /**
   * Parses a read command and enqueues it.
   *
   * @param topic Received topic.
   * @param payload Payload bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _handleMessage(const char* topic, const uint8_t* payload,
                      size_t length);

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
   * Publishes one sensor topic.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param payload Text payload.
   * @param retained Retained-message flag.
   * @return True when publication was accepted.
   */
  bool _publish(uint8_t moduleSlot, uint8_t sensorIndex, const char* payload,
                bool retained);

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
  bool _formatSample(uint8_t moduleSlot, uint8_t sensorIndex, char* out,
                     size_t outCap) const;

  /**
   * Formats a successful reading payload.
   *
   * @param connected Presence flag.
   * @param value Raw value, used only when connected is true.
   * @param out Destination buffer.
   * @param outCap Destination capacity.
   * @return Nothing.
   */
  static void _formatReading(bool connected, int32_t value, char* out,
                             size_t outCap);
};

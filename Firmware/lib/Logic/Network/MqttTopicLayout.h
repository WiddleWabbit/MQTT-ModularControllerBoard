#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "MqttService.h"

/**
 * Builds the MQTT topic tree for one controller board.
 *
 * The device id is a single path segment. Slot 1 is "{id}/slot/1".
 * Command topics are "{id}/solenoids", "{id}/solenoids/connected",
 * "{id}/pump", "{id}/sensor/read", and "{id}/sensor/connected". The
 * id itself is chosen by the application; this object does not invent
 * a default.
 */
class MqttTopicLayout
{
public:
  /**
   * Number of command topics subscribed at QoS 1.
   */
  static const size_t kSubscriptionCount = 5;

  /**
   * Creates a layout for one device id.
   *
   * @param deviceId Topic root. A null id is stored as empty.
   *        Generation starts at 1.
   */
  explicit MqttTopicLayout(const char* deviceId);

  /**
   * Replaces the device id and rebuilds every topic.
   *
   * The same id, including null and empty as the same value, leaves
   * the generation unchanged. A different id adds 1 to the generation
   * so publishers send their current values on the new topics.
   *
   * @param deviceId Topic root. A null id is stored as empty.
   * @return Nothing.
   */
  void setDeviceId(const char* deviceId);

  /**
   * Returns the current device id.
   *
   * @return Device id. Empty when none was supplied.
   */
  const char* deviceId() const;

  /**
   * Returns the generation of the current id.
   *
   * @return Counter starting at 1. It increases when the id changes.
   */
  uint32_t generation() const;

  /**
   * Returns the slot topic stem.
   *
   * @return "{id}/slot".
   */
  const char* slotPrefix() const;

  /**
   * Returns the solenoid desired-state topic.
   *
   * @return "{id}/solenoids".
   */
  const char* solenoidCommand() const;

  /**
   * Returns the solenoid connected-output query topic.
   *
   * @return "{id}/solenoids/connected".
   */
  const char* solenoidConnected() const;

  /**
   * Returns the pump command topic.
   *
   * @return "{id}/pump".
   */
  const char* pumpCommand() const;

  /**
   * Returns the immediate sensor-read topic.
   *
   * @return "{id}/sensor/read".
   */
  const char* sensorRead() const;

  /**
   * Returns the sensor connected-input query topic.
   *
   * @return "{id}/sensor/connected".
   */
  const char* sensorConnected() const;

  /**
   * Returns the five QoS 1 command subscriptions.
   *
   * The pointer stays valid until the next setDeviceId call. Order is
   * solenoids, solenoids/connected, pump, sensor/read, then
   * sensor/connected.
   *
   * @return Subscription array of kSubscriptionCount entries.
   */
  const MqttSubscription* subscriptions() const;

  /**
   * Returns the number of command subscriptions.
   *
   * @return kSubscriptionCount.
   */
  size_t subscriptionCount() const;

private:
  std::string _deviceId;
  std::string _slotPrefix;
  std::string _solenoidCommand;
  std::string _solenoidConnected;
  std::string _pumpCommand;
  std::string _sensorRead;
  std::string _sensorConnected;
  MqttSubscription _subscriptions[kSubscriptionCount];
  uint32_t _generation;

  /**
   * Rewrites topic strings and subscription pointers from the id.
   *
   * @return Nothing.
   */
  void _rebuild();
};

#include "MqttTopicLayout.h"

// ========== Construction ==========

MqttTopicLayout::MqttTopicLayout(const char* deviceId)
  : _generation(1)
{
  _deviceId = deviceId == nullptr ? "" : deviceId;
  _rebuild();
}


// ========== Public API ==========

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
void MqttTopicLayout::setDeviceId(const char* deviceId)
{
  const std::string next = deviceId == nullptr ? "" : deviceId;
  if (next == _deviceId)
  {
    return;
  }
  _deviceId = next;
  _rebuild();
  ++_generation;
}

/**
 * Returns the current device id.
 *
 * @return Device id. Empty when none was supplied.
 */
const char* MqttTopicLayout::deviceId() const
{
  return _deviceId.c_str();
}

/**
 * Returns the generation of the current id.
 *
 * @return Counter starting at 1. It increases when the id changes.
 */
uint32_t MqttTopicLayout::generation() const
{
  return _generation;
}

/**
 * Returns the slot topic stem.
 *
 * @return "{id}/slot".
 */
const char* MqttTopicLayout::slotPrefix() const
{
  return _slotPrefix.c_str();
}

/**
 * Returns the solenoid desired-state topic.
 *
 * @return "{id}/solenoids".
 */
const char* MqttTopicLayout::solenoidCommand() const
{
  return _solenoidCommand.c_str();
}

/**
 * Returns the solenoid connected-output query topic.
 *
 * @return "{id}/solenoids/connected".
 */
const char* MqttTopicLayout::solenoidConnected() const
{
  return _solenoidConnected.c_str();
}

/**
 * Returns the pump command topic.
 *
 * @return "{id}/pump".
 */
const char* MqttTopicLayout::pumpCommand() const
{
  return _pumpCommand.c_str();
}

/**
 * Returns the immediate sensor-read topic.
 *
 * @return "{id}/sensor/read".
 */
const char* MqttTopicLayout::sensorRead() const
{
  return _sensorRead.c_str();
}

/**
 * Returns the sensor connected-input query topic.
 *
 * @return "{id}/sensor/connected".
 */
const char* MqttTopicLayout::sensorConnected() const
{
  return _sensorConnected.c_str();
}

/**
 * Returns the five QoS 1 command subscriptions.
 *
 * The pointer stays valid until the next setDeviceId call. Order is
 * solenoids, solenoids/connected, pump, sensor/read, then
 * sensor/connected.
 *
 * @return Subscription array of kSubscriptionCount entries.
 */
const MqttSubscription* MqttTopicLayout::subscriptions() const
{
  return _subscriptions;
}

/**
 * Returns the number of command subscriptions.
 *
 * @return kSubscriptionCount.
 */
size_t MqttTopicLayout::subscriptionCount() const
{
  return kSubscriptionCount;
}


// ========== Topic Strings ==========

/**
 * Rewrites topic strings and subscription pointers from the id.
 *
 * @return Nothing.
 */
void MqttTopicLayout::_rebuild()
{
  _slotPrefix = _deviceId + "/slot";
  _solenoidCommand = _deviceId + "/solenoids";
  _solenoidConnected = _deviceId + "/solenoids/connected";
  _pumpCommand = _deviceId + "/pump";
  _sensorRead = _deviceId + "/sensor/read";
  _sensorConnected = _deviceId + "/sensor/connected";
  _subscriptions[0] = {_solenoidCommand.c_str(), 1};
  _subscriptions[1] = {_solenoidConnected.c_str(), 1};
  _subscriptions[2] = {_pumpCommand.c_str(), 1};
  _subscriptions[3] = {_sensorRead.c_str(), 1};
  _subscriptions[4] = {_sensorConnected.c_str(), 1};
}

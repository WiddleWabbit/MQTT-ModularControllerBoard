#include "MqttManager.h"

MqttManager::MqttManager(IWifiStation& wifi, IClock& clock,
                         IMqttClient& client, const MqttConfig& config)
  : _wifi(wifi),
    _clock(clock),
    _client(client),
    _config(config)
{
  _client.setMessageCallback(routeMessage, this);
}

bool MqttManager::update()
{
  if (!_wifi.isConnected()) {
    if (_client.isConnected()) {
      _client.disconnect();
    }
    _state = MqttConnectionState::WaitingForWifi;
    return false;
  }

  if (_client.isConnected()) {
    _state = MqttConnectionState::Connected;
    _client.loop();
    return true;
  }

  const unsigned long now = _clock.nowMillis();
  const bool readyToAttempt =
      !_hasAttempted ||
      (now - _lastAttemptAt >= _config.reconnectIntervalMs);
  if (!readyToAttempt) {
    _state = MqttConnectionState::Backoff;
    return false;
  }

  return attemptConnection(now);
}

bool MqttManager::attemptConnection(unsigned long now)
{
  _hasAttempted = true;
  _lastAttemptAt = now;
  if (!_client.connect(_config.clientId.c_str(), _config.username.c_str(),
                       _config.password.c_str())) {
    _state = MqttConnectionState::Backoff;
    return false;
  }

  for (const std::string& topic : _subscriptions) {
    if (!_client.subscribe(topic.c_str())) {
      _client.disconnect();
      _state = MqttConnectionState::Backoff;
      return false;
    }
  }

  _state = MqttConnectionState::Connected;
  return true;
}

bool MqttManager::addSubscription(const char* topic)
{
  if (topic == nullptr || topic[0] == '\0') {
    return false;
  }

  _subscriptions.emplace_back(topic);
  if (_client.isConnected()) {
    return _client.subscribe(topic);
  }
  return true;
}

bool MqttManager::publish(const char* topic, const char* payload,
                          bool retained)
{
  if (!_client.isConnected() || topic == nullptr || topic[0] == '\0' ||
      payload == nullptr) {
    return false;
  }
  return _client.publish(topic, payload, retained);
}

bool MqttManager::setMessageHandler(MqttManagerMessageHandler handler)
{
  if (handler == nullptr) {
    return false;
  }
  _messageHandler = handler;
  return true;
}

MqttConnectionState MqttManager::state() const
{
  return _state;
}

bool MqttManager::isConnected() const
{
  return _state == MqttConnectionState::Connected &&
         _client.isConnected();
}

bool MqttManager::isValidConfiguration(const MqttConfig& config)
{
  return !config.host.empty() && config.port != 0 && !config.clientId.empty() &&
         config.reconnectIntervalMs != 0;
}

void MqttManager::routeMessage(void* context, const char* topic,
                               const unsigned char* payload,
                               unsigned int payloadLength)
{
  MqttManager* manager = static_cast<MqttManager*>(context);
  if (manager == nullptr || manager->_messageHandler == nullptr) {
    return;
  }
  manager->_messageHandler(
      topic, reinterpret_cast<const char*>(payload), payloadLength);
}

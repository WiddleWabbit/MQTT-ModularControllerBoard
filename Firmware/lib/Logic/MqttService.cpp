#include "MqttService.h"

// ========== Construction ==========

MqttService::MqttService(IMqttClient& client, IClock& clock,
                         const MqttConfig& config)
  : _client(client),
    _clock(clock),
    _config(config),
    _retryDelayMs(config.initialRetryDelayMs)
{
}


// ========== Public API ==========

/**
 * Installs the application inbound-message handler.
 *
 * @param callback Handler function, or nullptr to ignore messages.
 * @param context Opaque callback context.
 * @return Nothing.
 */
void MqttService::setMessageHandler(MqttMessageCallback callback, void* context)
{
  _messageHandler = callback;
  _messageContext = context;
}

/**
 * Starts MQTT service management.
 *
 * @return Nothing.
 */
void MqttService::begin()
{
  _client.setCallback(_handleClientMessage, this);
  _retryDelayMs = _config.initialRetryDelayMs;
  _retryAvailableAt = _clock.millis();
  _state = MqttServiceState::WaitingForNetwork;
}

/**
 * Advances MQTT connection and transport servicing.
 *
 * @param networkReady True when the WiFi service is connected.
 * @return Nothing.
 */
void MqttService::update(bool networkReady)
{
  if (_state == MqttServiceState::Idle)
  {
    return;
  }

  if (!networkReady)
  {
    if (_client.connected())
    {
      _client.disconnect();
    }
    _state = MqttServiceState::WaitingForNetwork;
    _retryAvailableAt = _clock.millis();
    return;
  }

  if (_state == MqttServiceState::Connected)
  {
    if (!_client.connected())
    {
      _scheduleRetry();
      return;
    }
    _client.loop();
    return;
  }

  const uint32_t now = _clock.millis();
  if (_state == MqttServiceState::Backoff &&
      !_isDue(now, _retryAvailableAt))
  {
    return;
  }

  _state = MqttServiceState::Connecting;
  if (!_client.connect(_config.clientId, _config.username, _config.password))
  {
    _scheduleRetry();
    return;
  }

  if (!_subscribeConfiguredTopics())
  {
    _client.disconnect();
    _scheduleRetry();
    return;
  }

  _state = MqttServiceState::Connected;
  _retryDelayMs = _config.initialRetryDelayMs;
}

/**
 * Publishes a message when the broker is connected.
 *
 * @param topic Topic name.
 * @param payload Message payload.
 * @param retained Retained-message flag.
 * @return True when publication was accepted.
 */
bool MqttService::publish(const char* topic, const char* payload, bool retained)
{
  if (_state != MqttServiceState::Connected || !_client.connected())
  {
    return false;
  }
  return _client.publish(topic, payload, retained);
}

/**
 * Reads the current service state.
 *
 * @return Current state.
 */
MqttServiceState MqttService::state() const
{
  return _state;
}


// ========== Private Helpers ==========

/**
 * Handles a callback from the injected MQTT client.
 *
 * @param topic Topic name.
 * @param payload Message bytes.
 * @param length Payload length.
 * @param context MqttService instance.
 * @return Nothing.
 */
void MqttService::_handleClientMessage(const char* topic, const uint8_t* payload,
                                       size_t length, void* context)
{
  MqttService* service = static_cast<MqttService*>(context);
  if (service != nullptr && service->_messageHandler != nullptr)
  {
    service->_messageHandler(topic, payload, length, service->_messageContext);
  }
}

/**
 * Installs every configured topic subscription.
 *
 * @return True when all subscriptions were accepted.
 */
bool MqttService::_subscribeConfiguredTopics()
{
  for (size_t index = 0; index < _config.subscriptionCount; ++index)
  {
    const MqttSubscription& subscription = _config.subscriptions[index];
    if (!_client.subscribe(subscription.topic, subscription.qos))
    {
      return false;
    }
  }
  return true;
}

/**
 * Schedules a reconnect and advances exponential backoff.
 *
 * @return Nothing.
 */
void MqttService::_scheduleRetry()
{
  const uint32_t now = _clock.millis();
  _retryAvailableAt = now + _retryDelayMs;
  _state = MqttServiceState::Backoff;

  const uint32_t doubledDelay = _retryDelayMs * 2U;
  if (doubledDelay < _retryDelayMs ||
      doubledDelay > _config.maxRetryDelayMs)
  {
    _retryDelayMs = _config.maxRetryDelayMs;
  }
  else
  {
    _retryDelayMs = doubledDelay;
  }
}

/**
 * Tests whether an absolute retry time has been reached.
 *
 * @param now Current monotonic time.
 * @param due Retry time.
 * @return True when retry time is due.
 */
bool MqttService::_isDue(uint32_t now, uint32_t due)
{
  return static_cast<int32_t>(now - due) >= 0;
}

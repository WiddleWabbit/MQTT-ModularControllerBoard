#include "PubSubClientAdapter.h"

PubSubClientAdapter* PubSubClientAdapter::_activeAdapter = nullptr;

// ========== Construction ==========

/**
 * Creates an adapter around an existing PubSubClient.
 *
 * @param client PubSubClient instance with its transport configured.
 */
PubSubClientAdapter::PubSubClientAdapter(PubSubClient& client)
  : _client(client)
{
}

// ========== Public API ==========

/**
 * Configures the broker endpoint.
 *
 * @param host Broker hostname or address.
 * @param port Broker TCP port.
 * @return Nothing.
 */
void PubSubClientAdapter::setServer(const char* host, uint16_t port)
{
  _client.setServer(host, port);
}

/**
 * Installs the inbound message callback.
 *
 * @param callback Callback function.
 * @param context Opaque callback context.
 * @return Nothing.
 */
void PubSubClientAdapter::setCallback(MqttMessageCallback callback, void* context)
{
  _callback = callback;
  _context = context;
  _activeAdapter = this;
  _client.setCallback(_handleMessage);
}

/**
 * Attempts one broker connection.
 *
 * @param clientId MQTT client identifier.
 * @param username Optional username.
 * @param password Optional password.
 * @return True when connected.
 */
bool PubSubClientAdapter::connect(const char* clientId, const char* username,
                                  const char* password)
{
  if (username == nullptr || password == nullptr)
  {
    return _client.connect(clientId);
  }
  return _client.connect(clientId, username, password);
}

/**
 * Reports broker connection state.
 *
 * @return True when connected.
 */
bool PubSubClientAdapter::connected() const
{
  return _client.connected();
}

/**
 * Disconnects from the broker.
 *
 * @return Nothing.
 */
void PubSubClientAdapter::disconnect()
{
  _client.disconnect();
}

/**
 * Subscribes to an MQTT topic filter.
 *
 * @param topic Topic filter.
 * @param qos Requested quality of service.
 * @return True when accepted.
 */
bool PubSubClientAdapter::subscribe(const char* topic, uint8_t qos)
{
  return _client.subscribe(topic, qos);
}

/**
 * Publishes an MQTT message.
 *
 * @param topic Topic name.
 * @param payload Message payload.
 * @param retained Retained-message flag.
 * @return True when accepted.
 */
bool PubSubClientAdapter::publish(const char* topic, const char* payload,
                                  bool retained)
{
  return _client.publish(topic, payload, retained);
}

/**
 * Services PubSubClient network processing.
 *
 * @return Nothing.
 */
void PubSubClientAdapter::loop()
{
  _client.loop();
}

// ========== Private Helpers ==========

/**
 * Bridges PubSubClient's callback signature to IMqttClient.
 *
 * @param topic Topic name.
 * @param payload Message bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void PubSubClientAdapter::_onMessage(char* topic, uint8_t* payload,
                                     unsigned int length)
{
  if (_callback != nullptr)
  {
    _callback(topic, payload, length, _context);
  }
}

/**
 * Static callback thunk passed to PubSubClient.
 *
 * @param topic Topic name.
 * @param payload Message bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void PubSubClientAdapter::_handleMessage(char* topic, uint8_t* payload,
                                         unsigned int length)
{
  if (_activeAdapter != nullptr)
  {
    _activeAdapter->_onMessage(topic, payload, length);
  }
}

#include "Esp32MqttClient.h"

Esp32MqttClient* Esp32MqttClient::_activeInstance = nullptr;

Esp32MqttClient::Esp32MqttClient(Client& networkClient, const char* host,
                                 unsigned int port)
  : _client(networkClient)
{
  _client.setServer(host, port);
  _activeInstance = this;
}

void Esp32MqttClient::setMessageCallback(MqttMessageCallback callback,
                                         void* context)
{
  _callback = callback;
  _context = context;
  _client.setCallback(routeMessage);
}

bool Esp32MqttClient::connect(const char* clientId, const char* username,
                              const char* password)
{
  return _client.connect(clientId, username, password);
}

void Esp32MqttClient::disconnect()
{
  _client.disconnect();
}

bool Esp32MqttClient::isConnected() const
{
  return const_cast<PubSubClient&>(_client).connected();
}

bool Esp32MqttClient::publish(const char* topic, const char* payload,
                              bool retained)
{
  return _client.publish(topic, payload, retained);
}

bool Esp32MqttClient::subscribe(const char* topic)
{
  return _client.subscribe(topic);
}

void Esp32MqttClient::loop()
{
  _client.loop();
}

void Esp32MqttClient::routeMessage(char* topic, unsigned char* payload,
                                   unsigned int payloadLength)
{
  if (_activeInstance != nullptr && _activeInstance->_callback != nullptr) {
    _activeInstance->_callback(_activeInstance->_context, topic, payload,
                               payloadLength);
  }
}

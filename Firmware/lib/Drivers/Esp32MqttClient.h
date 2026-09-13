#pragma once

#include <PubSubClient.h>

#include "IMqttClient.h"

class Esp32MqttClient : public IMqttClient {
public:
  /**
   * Creates a PubSubClient adapter using an injected Arduino network client.
   *
   * @param networkClient Connected-capable Arduino TCP client.
   * @param host MQTT broker host name.
   * @param port MQTT broker port.
   */
  Esp32MqttClient(Client& networkClient, const char* host,
                  unsigned int port = 1883);

  /**
   * Registers the PubSubClient message callback adapter.
   *
   * @param callback Application callback.
   * @param context Opaque application context.
   * @return Nothing.
   */
  void setMessageCallback(MqttMessageCallback callback,
                          void* context) override;

  /**
   * Connects to the configured broker using client credentials.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return True when connected.
   */
  bool connect(const char* clientId, const char* username,
               const char* password) override;

  /**
   * Disconnects the PubSubClient session.
   *
   * @return Nothing.
   */
  void disconnect() override;

  /**
   * Reports the PubSubClient connection state.
   *
   * @return True when connected.
   */
  bool isConnected() const override;

  /**
   * Publishes a retained or non-retained payload.
   *
   * @param topic Destination topic.
   * @param payload Null-terminated payload.
   * @param retained Retain flag.
   * @return True when accepted by PubSubClient.
   */
  bool publish(const char* topic, const char* payload,
               bool retained) override;

  /**
   * Subscribes to a broker topic filter.
   *
   * @param topic Topic filter.
   * @return True when accepted by PubSubClient.
   */
  bool subscribe(const char* topic) override;

  /**
   * Services PubSubClient keep-alive and incoming traffic.
   *
   * @return Nothing.
   */
  void loop() override;

private:
 /**
  * Adapts the PubSubClient callback to the hardware-independent callback.
  *
  * @param topic Incoming MQTT topic.
  * @param payload Incoming payload bytes.
  * @param payloadLength Number of payload bytes.
  * @return Nothing.
  */
 static void routeMessage(char* topic, unsigned char* payload,
                          unsigned int payloadLength);
  static Esp32MqttClient* _activeInstance;

  PubSubClient _client;
  MqttMessageCallback _callback = nullptr;
  void* _context = nullptr;
};

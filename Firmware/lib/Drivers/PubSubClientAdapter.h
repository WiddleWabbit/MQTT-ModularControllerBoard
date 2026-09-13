#pragma once

#include <PubSubClient.h>

#include "IMqttClient.h"

/**
 * Adapts PubSubClient to the platform-independent IMqttClient interface.
 */
class PubSubClientAdapter : public IMqttClient
{
public:
  /**
   * Creates an adapter around an existing PubSubClient.
   *
   * @param client PubSubClient instance with its transport configured.
   */
  explicit PubSubClientAdapter(PubSubClient& client);

  /**
   * Configures the broker endpoint.
   *
   * @param host Broker hostname or address.
   * @param port Broker TCP port.
   * @return Nothing.
   */
  void setServer(const char* host, uint16_t port);

  /**
   * Installs the inbound message callback.
   *
   * @param callback Callback function.
   * @param context Opaque callback context.
   * @return Nothing.
   */
  void setCallback(MqttMessageCallback callback, void* context) override;

  /**
   * Attempts one broker connection.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return True when connected.
   */
  bool connect(const char* clientId, const char* username,
               const char* password) override;

  /**
   * Reports broker connection state.
   *
   * @return True when connected.
   */
  bool connected() const override;

  /**
   * Disconnects from the broker.
   *
   * @return Nothing.
   */
  void disconnect() override;

  /**
   * Subscribes to an MQTT topic filter.
   *
   * @param topic Topic filter.
   * @param qos Requested quality of service.
   * @return True when accepted.
   */
  bool subscribe(const char* topic, uint8_t qos) override;

  /**
   * Publishes an MQTT message.
   *
   * @param topic Topic name.
   * @param payload Message payload.
   * @param retained Retained-message flag.
   * @return True when accepted.
   */
  bool publish(const char* topic, const char* payload, bool retained) override;

  /**
   * Services PubSubClient network processing.
   *
   * @return Nothing.
   */
  void loop() override;

private:
  PubSubClient& _client;
  MqttMessageCallback _callback = nullptr;
  void* _context = nullptr;

  /**
   * Bridges PubSubClient's callback signature to IMqttClient.
   *
   * @param topic Topic name.
   * @param payload Message bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  void _onMessage(char* topic, uint8_t* payload, unsigned int length);

  /**
   * Static callback thunk passed to PubSubClient.
   *
   * @param topic Topic name.
   * @param payload Message bytes.
   * @param length Payload length.
   * @return Nothing.
   */
  static void _handleMessage(char* topic, uint8_t* payload,
                             unsigned int length);

  static PubSubClientAdapter* _activeAdapter;
};

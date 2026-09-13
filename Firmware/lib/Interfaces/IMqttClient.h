#pragma once

using MqttMessageCallback = void (*)(void* context, const char* topic,
                                     const unsigned char* payload,
                                     unsigned int payloadLength);

class IMqttClient {
public:
  /**
   * Releases the interface without owning a concrete MQTT client.
   *
   * @return Nothing.
   */
  virtual ~IMqttClient() = default;

  /**
   * Registers the callback used for incoming MQTT messages.
   *
   * @param callback Callback function.
   * @param context Opaque callback context.
   * @return Nothing.
   */
  virtual void setMessageCallback(MqttMessageCallback callback,
                                  void* context) = 0;

  /**
   * Connects to the configured MQTT broker.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return True when connected.
   */
  virtual bool connect(const char* clientId, const char* username,
                       const char* password) = 0;

  /**
   * Closes the broker connection.
   *
   * @return Nothing.
   */
  virtual void disconnect() = 0;

  /**
   * Reports the current broker connection state.
   *
   * @return True when connected.
   */
  virtual bool isConnected() const = 0;

  /**
   * Publishes a null-terminated payload.
   *
   * @param topic Destination topic.
   * @param payload Null-terminated message payload.
   * @param retained Whether the broker should retain the message.
   * @return True when accepted by the client.
   */
  virtual bool publish(const char* topic, const char* payload,
                       bool retained) = 0;

  /**
   * Subscribes to a topic.
   *
   * @param topic Topic filter.
   * @return True when accepted by the client.
   */
  virtual bool subscribe(const char* topic) = 0;

  /**
   * Services the MQTT connection without blocking.
   *
   * @return Nothing.
   */
  virtual void loop() = 0;
};

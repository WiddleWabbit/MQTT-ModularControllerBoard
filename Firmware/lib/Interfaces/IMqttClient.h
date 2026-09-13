#pragma once

#include <cstddef>
#include <cstdint>

using MqttMessageCallback = void (*)(const char* topic, const uint8_t* payload,
                                     size_t length, void* context);

/**
 * Abstracts an MQTT client library and its transport.
 */
class IMqttClient
{
public:
  virtual ~IMqttClient() = default;

  /**
   * Installs the inbound message callback.
   *
   * @param callback Callback function, or nullptr to disable callbacks.
   * @param context Opaque callback context.
   * @return Nothing.
   */
  virtual void setCallback(MqttMessageCallback callback, void* context) = 0;

  /**
   * Attempts one broker connection.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return True when the broker accepted the connection.
   */
  virtual bool connect(const char* clientId, const char* username,
                       const char* password) = 0;

  /**
   * Reports broker connection state.
   *
   * @return True when connected.
   */
  virtual bool connected() const = 0;

  /**
   * Closes the broker connection.
   *
   * @return Nothing.
   */
  virtual void disconnect() = 0;

  /**
   * Subscribes to a topic filter.
   *
   * @param topic Topic filter.
   * @param qos Requested quality of service.
   * @return True when the subscription was accepted.
   */
  virtual bool subscribe(const char* topic, uint8_t qos) = 0;

  /**
   * Publishes a message.
   *
   * @param topic Topic name.
   * @param payload Message payload.
   * @param retained Retained-message flag.
   * @return True when the publication was accepted.
   */
  virtual bool publish(const char* topic, const char* payload,
                       bool retained) = 0;

  /**
   * Services the MQTT transport and dispatches inbound messages.
   *
   * @return Nothing.
   */
  virtual void loop() = 0;
};

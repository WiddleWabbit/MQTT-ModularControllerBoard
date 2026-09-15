#pragma once

#include <cstddef>
#include <cstdint>

#include "IClock.h"
#include "IMqttClient.h"

struct MqttSubscription
{
  const char* topic;
  uint8_t qos;
};

struct MqttConfig
{
  const char* clientId;
  const char* username;
  const char* password;
  const MqttSubscription* subscriptions;
  size_t subscriptionCount;
  uint32_t initialRetryDelayMs;
  uint32_t maxRetryDelayMs;
};

enum class MqttServiceState : uint8_t
{
  Idle,
  WaitingForNetwork,
  Connecting,
  Connected,
  Backoff
};

/**
 * Owns nonblocking MQTT connection, subscription, and message dispatch logic.
 */
class MqttService
{
public:
  /**
   * Creates an MQTT service using injected client and clock.
   *
   * @param client MQTT client interface.
   * @param clock Monotonic clock.
   * @param config Client credentials, subscriptions, and retry settings.
   */
  MqttService(IMqttClient& client, IClock& clock, const MqttConfig& config);

  /**
   * Installs the application inbound-message handler.
   *
   * @param callback Handler function, or nullptr to ignore messages.
   * @param context Opaque callback context.
   * @return Nothing.
   */
  void setMessageHandler(MqttMessageCallback callback, void* context);

  /**
   * Starts MQTT service management.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Advances MQTT connection and transport servicing.
   *
   * @param networkReady True when the WiFi service is connected.
   * @return Nothing.
   */
  void update(bool networkReady);

  /**
   * Replaces broker settings and forces a fresh connection sequence.
   *
   * @param config New broker configuration.
   * @return Nothing.
   */
  void reconfigure(const MqttConfig& config);

  /**
   * Changes the broker endpoint for subsequent connections.
   *
   * @param host Broker hostname.
   * @param port Broker TCP port.
   * @return Nothing.
   */
  void setBroker(const char* host, uint16_t port);

  /**
   * Returns the active broker configuration.
   *
   * @return Active configuration.
   */
  const MqttConfig& config() const;

  /**
   * Publishes a message when the broker is connected.
   *
   * @param topic Topic name.
   * @param payload Message payload.
   * @param retained Retained-message flag.
   * @return True when publication was accepted.
   */
  bool publish(const char* topic, const char* payload, bool retained);

  /**
   * Reads the current service state.
   *
   * @return Current state.
   */
  MqttServiceState state() const;

private:
  IMqttClient& _client;
  IClock& _clock;
  MqttConfig _config;
  MqttServiceState _state = MqttServiceState::Idle;
  MqttMessageCallback _messageHandler = nullptr;
  void* _messageContext = nullptr;
  uint32_t _retryAvailableAt = 0;
  uint32_t _retryDelayMs = 0;

  /**
   * Handles a callback from the injected MQTT client.
   *
   * @param topic Topic name.
   * @param payload Message bytes.
   * @param length Payload length.
   * @param context MqttService instance.
   * @return Nothing.
   */
  static void _handleClientMessage(const char* topic, const uint8_t* payload,
                                   size_t length, void* context);

  /**
   * Installs all configured subscriptions after a broker connection.
   *
   * @return True when every subscription was accepted.
   */
  bool _subscribeConfiguredTopics();

  /**
   * Schedules a reconnect and advances exponential backoff.
   *
   * @return Nothing.
   */
  void _scheduleRetry();

  /**
   * Tests whether an absolute retry time has been reached.
   *
   * @param now Current monotonic time.
   * @param due Retry time.
   * @return True when retry time is due.
   */
  static bool _isDue(uint32_t now, uint32_t due);
};

#pragma once

#include <string>
#include <vector>

#include "IClock.h"
#include "IMqttClient.h"
#include "IWifiStation.h"

struct MqttConfig {
  std::string host;
  unsigned int port = 1883;
  std::string clientId;
  std::string username;
  std::string password;
  unsigned long reconnectIntervalMs = 5000;
};

enum class MqttConnectionState {
  WaitingForWifi,
  Backoff,
  Connected
};

using MqttManagerMessageHandler = void (*)(const char* topic,
                                            const char* payload,
                                            unsigned int payloadLength);

class MqttManager {
public:
  /**
   * Creates a WiFi-aware, non-blocking MQTT coordinator.
   *
   * @param wifi Station interface used as the network readiness gate.
   * @param clock Monotonic clock used for reconnect backoff.
   * @param client MQTT transport interface.
   * @param config Broker and client configuration.
   */
  MqttManager(IWifiStation& wifi, IClock& clock, IMqttClient& client,
              const MqttConfig& config);

  /**
   * Advances connection, subscription, callback, and keep-alive processing.
   *
   * @return True when connected after this update; otherwise false.
   */
  bool update();

  /**
   * Adds a topic to subscribe after each successful connection.
   *
   * @param topic Non-empty MQTT topic filter.
   * @return True when accepted.
   */
  bool addSubscription(const char* topic);

  /**
   * Publishes a message when the manager is connected.
   *
   * @param topic Destination topic.
   * @param payload Null-terminated payload.
   * @param retained MQTT retain flag.
   * @return True when published.
   */
  bool publish(const char* topic, const char* payload, bool retained = false);

  /**
   * Installs the application message route.
   *
   * @param handler Non-null application callback.
   * @return True when accepted.
   */
  bool setMessageHandler(MqttManagerMessageHandler handler);

  /**
   * Returns the current connection state.
   *
   * @return Current MQTT state.
   */
  MqttConnectionState state() const;

  /**
   * Reports whether MQTT is currently connected.
   *
   * @return True when connected.
   */
  bool isConnected() const;

  /**
   * Validates required MQTT configuration.
   *
   * @param config Configuration to validate.
   * @return True when usable.
   */
  static bool isValidConfiguration(const MqttConfig& config);

private:
 /**
  * Routes a transport callback to the configured application handler.
  *
  * @param context Manager instance supplied during callback registration.
  * @param topic Incoming MQTT topic.
  * @param payload Incoming payload bytes.
  * @param payloadLength Number of payload bytes.
  * @return Nothing.
  */
 static void routeMessage(void* context, const char* topic,
                          const unsigned char* payload,
                          unsigned int payloadLength);

 /**
  * Attempts one broker connection and restores subscriptions.
  *
  * @param now Current monotonic time in milliseconds.
  * @return True when connected and all subscriptions succeeded.
  */
 bool attemptConnection(unsigned long now);

  IWifiStation& _wifi;
  IClock& _clock;
  IMqttClient& _client;
  MqttConfig _config;
  std::vector<std::string> _subscriptions;
  MqttManagerMessageHandler _messageHandler = nullptr;
  MqttConnectionState _state = MqttConnectionState::WaitingForWifi;
  unsigned long _lastAttemptAt = 0;
  bool _hasAttempted = false;
};

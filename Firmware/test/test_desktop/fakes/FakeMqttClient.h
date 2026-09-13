#pragma once

#include <string>
#include <deque>
#include <vector>

#include "IMqttClient.h"

class FakeMqttClient : public IMqttClient {
public:
  struct PublishedMessage {
    std::string topic;
    std::string payload;
    bool retained = false;
  };

  /**
   * Installs the callback and records the registration.
   *
   * @param callback Message callback function.
   * @param context Opaque callback context.
   * @return Nothing.
   */
  void setMessageCallback(MqttMessageCallback callback,
                           void* context) override
  {
    messageCallback = callback;
    callbackContext = context;
    ++setCallbackCount;
  }

  /**
   * Attempts a controllable MQTT connection.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return Configured connection result.
   */
  bool connect(const char* clientId, const char* username,
               const char* password) override
  {
    ++connectCount;
    lastClientId = clientId == nullptr ? "" : clientId;
    lastUsername = username == nullptr ? "" : username;
    lastPassword = password == nullptr ? "" : password;
    const bool result = connectResults.empty() ? connectResult
                                               : connectResults.front();
    if (!connectResults.empty()) {
      connectResults.pop_front();
    }
    connected = result;
    return result;
  }

  /**
   * Records a disconnect request and clears the connection.
   *
   * @return Nothing.
   */
  void disconnect() override
  {
    ++disconnectCount;
    connected = false;
  }

  /**
   * Reports the controllable connection state.
   *
   * @return True when the fake is connected.
   */
  bool isConnected() const override
  {
    return connected;
  }

  /**
   * Records a publish operation.
   *
   * @param topic MQTT topic.
   * @param payload Message payload.
   * @param retained Retain flag.
   * @return Configured publish result.
   */
  bool publish(const char* topic, const char* payload,
               bool retained) override
  {
    ++publishCount;
    published.push_back({topic == nullptr ? "" : topic,
                         payload == nullptr ? "" : payload, retained});
    const bool result = publishResults.empty() ? publishResult
                                               : publishResults.front();
    if (!publishResults.empty()) {
      publishResults.pop_front();
    }
    return result;
  }

  /**
   * Records a subscription operation.
   *
   * @param topic MQTT topic.
   * @return Configured subscribe result.
   */
  bool subscribe(const char* topic) override
  {
    ++subscribeCount;
    subscribedTopics.push_back(topic == nullptr ? "" : topic);
    const bool result = subscribeResults.empty() ? subscribeResult
                                                 : subscribeResults.front();
    if (!subscribeResults.empty()) {
      subscribeResults.pop_front();
    }
    return result;
  }

  /**
   * Records one non-blocking MQTT service call.
   *
   * @return Nothing.
   */
  void loop() override
  {
    ++loopCount;
  }

  /**
   * Delivers a message through the registered callback.
   *
   * @param topic Message topic.
   * @param payload Message bytes.
   * @return Nothing.
   */
  void deliver(const char* topic, const char* payload)
  {
    if (messageCallback != nullptr) {
      messageCallback(callbackContext, topic,
                      reinterpret_cast<const unsigned char*>(payload),
                      payload == nullptr ? 0U
                                         : static_cast<unsigned int>(
                                               std::string(payload).size()));
    }
  }

  /**
   * Restores the fake to its initial state.
   *
   * @return Nothing.
   */
  void reset()
  {
    messageCallback = nullptr;
    callbackContext = nullptr;
    connectResult = true;
    connected = false;
    publishResult = true;
    subscribeResult = true;
    connectResults.clear();
    publishResults.clear();
    subscribeResults.clear();
    setCallbackCount = 0;
    connectCount = 0;
    disconnectCount = 0;
    publishCount = 0;
    subscribeCount = 0;
    loopCount = 0;
    lastClientId.clear();
    lastUsername.clear();
    lastPassword.clear();
    published.clear();
    subscribedTopics.clear();
  }

  MqttMessageCallback messageCallback = nullptr;
  void* callbackContext = nullptr;
  bool connectResult = true;
  bool connected = false;
  bool publishResult = true;
  bool subscribeResult = true;
  std::deque<bool> connectResults;
  std::deque<bool> publishResults;
  std::deque<bool> subscribeResults;
  unsigned int setCallbackCount = 0;
  unsigned int connectCount = 0;
  unsigned int disconnectCount = 0;
  unsigned int publishCount = 0;
  unsigned int subscribeCount = 0;
  unsigned int loopCount = 0;
  std::string lastClientId;
  std::string lastUsername;
  std::string lastPassword;
  std::vector<PublishedMessage> published;
  std::vector<std::string> subscribedTopics;
};

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "IMqttClient.h"

class FakeMqttClient : public IMqttClient
{
public:
  struct PublishedMessage
  {
    std::string topic;
    std::string payload;
    bool retained = false;
  };

  /**
   * Stores the callback used to deliver simulated inbound messages.
   *
   * @param callback Callback function.
   * @param context Callback context.
   * @return Nothing.
   */
  void setCallback(MqttMessageCallback callback, void* context) override
  {
    callbackFunction = callback;
    callbackContext = context;
  }

  /**
   * Attempts a simulated broker connection.
   *
   * @param clientId MQTT client identifier.
   * @param username Optional username.
   * @param password Optional password.
   * @return Configured connection result.
   */
  bool connect(const char* clientId, const char* username,
              const char* password) override
  {
    connectCallCount++;
    lastClientId = clientId == nullptr ? "" : clientId;
    lastUsername = username == nullptr ? "" : username;
    lastPassword = password == nullptr ? "" : password;
    const bool result = nextResult(connectResults, connectResult,
                                   connectResultIndex);
    connectedState = result;
    return result;
  }

  /**
   * Reports simulated broker connectivity.
   *
   * @return True when connected.
   */
  bool connected() const override
  {
    return connectedState;
  }

  /**
   * Disconnects the simulated broker.
   *
   * @return Nothing.
   */
  void disconnect() override
  {
    disconnectCallCount++;
    connectedState = false;
  }

  /**
   * Records a subscription request.
   *
   * @param topic Topic filter.
   * @param qos Requested QoS.
   * @return Configured subscription result.
   */
  bool subscribe(const char* topic, uint8_t qos) override
  {
    subscribeCallCount++;
    subscribedTopics.push_back(topic == nullptr ? "" : topic);
    subscribedQos.push_back(qos);
    return nextResult(subscribeResults, subscribeResult,
                      subscribeResultIndex);
  }

  /**
   * Records a publication request.
   *
   * @param topic Topic name.
   * @param payload Message payload.
   * @param retained Retained-message flag.
   * @return Configured publication result.
   */
  bool publish(const char* topic, const char* payload, bool retained) override
  {
    publishCallCount++;
    publishedMessages.push_back(
      {topic == nullptr ? "" : topic, payload == nullptr ? "" : payload, retained});
    return nextResult(publishResults, publishResult, publishResultIndex);
  }

  /**
   * Records servicing of the MQTT client.
   *
   * @return Nothing.
   */
  void loop() override
  {
    loopCallCount++;
  }

  /**
   * Delivers an inbound message to the registered callback.
   *
   * @param topic Topic name.
   * @param payload Message bytes.
   * @return Nothing.
   */
  void deliver(const char* topic, const std::string& payload)
  {
    if (callbackFunction != nullptr)
    {
      callbackFunction(topic, reinterpret_cast<const uint8_t*>(payload.data()),
                       payload.size(), callbackContext);
    }
  }

  bool connectResult = true;
  bool connectedState = false;
  bool subscribeResult = true;
  bool publishResult = true;
  int connectCallCount = 0;
  int disconnectCallCount = 0;
  int subscribeCallCount = 0;
  int publishCallCount = 0;
  int loopCallCount = 0;
  std::string lastClientId;
  std::string lastUsername;
  std::string lastPassword;
  std::vector<std::string> subscribedTopics;
  std::vector<uint8_t> subscribedQos;
  std::vector<PublishedMessage> publishedMessages;
  std::vector<bool> connectResults;
  std::vector<bool> subscribeResults;
  std::vector<bool> publishResults;

private:
  /**
   * Returns the next configured result, or the default when the sequence ends.
   *
   * @param sequence Optional result sequence.
   * @param defaultResult Fallback result.
   * @param index Mutable sequence cursor.
   * @return Next simulated operation result.
   */
  static bool nextResult(const std::vector<bool>& sequence, bool defaultResult,
                         size_t& index)
  {
    if (index < sequence.size())
    {
      return sequence[index++];
    }
    return defaultResult;
  }

  MqttMessageCallback callbackFunction = nullptr;
  void* callbackContext = nullptr;
  size_t connectResultIndex = 0;
  size_t subscribeResultIndex = 0;
  size_t publishResultIndex = 0;
};

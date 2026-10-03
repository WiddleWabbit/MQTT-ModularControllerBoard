#pragma once

#include <cstddef>

#include "IClock.h"
#include "IMqttClient.h"
#include "INtpAdapter.h"
#include "IPreferenceStore.h"
#include "IWifi.h"
#include "MqttService.h"
#include "MqttTopicLayout.h"
#include "NetworkRuntime.h"
#include "NtpService.h"
#include "PreferenceNetworkConfigStore.h"
#include "WifiManager.h"

class ModuleBus;
class SensorModule;
class SolenoidModule;
class PumpModule;
class SerialConsole;

/**
 * Retry timing for the Wi-Fi and MQTT state machines.
 * Passed in from the composition root.
 */
struct NetworkTiming
{
  uint32_t wifiConnectTimeoutMs;
  uint32_t wifiInitialRetryMs;
  uint32_t wifiMaxRetryMs;
  uint32_t mqttInitialRetryMs;
  uint32_t mqttMaxRetryMs;
};

/**
 * Wi-Fi, NTP, MQTT, and the persisted network record.
 * update() advances those state machines in order. Inbound MQTT
 * payloads are delivered to every handler registered with
 * addMessageHandler. Typed module commands are not decoded here.
 */
class Network
{
  friend class ModuleBus;
  friend class SensorModule;
  friend class SolenoidModule;
  friend class PumpModule;
  friend class SerialConsole;

public:
  static const size_t kMaxInboundHandlers = 4;

  /**
   * Creates the network module. Servers in ntpConfig must outlive it.
   * The MQTT handler fan-out is installed immediately.
   *
   * @param wifi Station adapter.
   * @param ntp Platform NTP adapter.
   * @param mqtt MQTT client adapter.
   * @param clock Monotonic clock.
   * @param preferences Key-value store for the network record.
   * @param defaultDeviceId Topic root used until a prefix is applied.
   *        This module does not invent that id.
   * @param ntpConfig NTP servers, offsets, and retry interval.
   * @param timing Wi-Fi and MQTT retry timing.
   */
  Network(IWifi& wifi, INtpAdapter& ntp, IMqttClient& mqtt, IClock& clock,
          IPreferenceStore& preferences, const char* defaultDeviceId,
          const NtpConfig& ntpConfig, const NetworkTiming& timing);

  /**
   * Loads the stored record over defaults, then starts Wi-Fi, MQTT,
   * and NTP. A missing record applies the defaults and returns false.
   *
   * @param defaults Values used for keys that are not stored.
   * @return True when at least one network key was stored.
   */
  bool begin(const NetworkConfig& defaults);

  /**
   * Advances Wi-Fi, then NTP, then MQTT. MQTT runs only while Wi-Fi
   * is connected. Does not block.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Publishes one payload when the broker is connected.
   *
   * @param topic Topic name.
   * @param payload Message payload.
   * @param retained Retained-message flag.
   * @return True when publication was accepted.
   */
  bool publish(const char* topic, const char* payload, bool retained);

  /**
   * Registers an inbound MQTT handler. Each handler sees every payload.
   * Handlers must not touch I2C. Registration is rejected when the
   * list is full or the callback is null.
   *
   * @param callback Handler function.
   * @param context Opaque callback context.
   * @return True when the handler was stored.
   */
  bool addMessageHandler(MqttMessageCallback callback, void* context);

  /**
   * Returns the boot warning from the last configuration load.
   *
   * @return Warning text, or an empty string.
   */
  const char* loadWarning() const;

  /**
   * Returns the active network configuration.
   *
   * @return Active configuration. Pointers stay valid until the next
   *         begin or apply.
   */
  const NetworkConfig& config() const;

  /**
   * Persists the selected fields and applies them to Wi-Fi or MQTT.
   *
   * @param config Values for the selected fields.
   * @param fields Fields to persist and apply.
   * @return True when persistence succeeds.
   */
  bool apply(const NetworkConfig& config, const NetworkConfigFieldMask& fields);

  /**
   * Reports whether a hostname or MQTT prefix can be stored.
   * A valid name is 1–31 characters, uses letters, digits, and hyphens,
   * and starts and ends with a letter or digit.
   *
   * @param name Candidate name.
   * @return True when the name is valid.
   */
  static bool isValidHostname(const char* name);

private:
  struct InboundHandler
  {
    MqttMessageCallback callback;
    void* context;
  };

  MqttTopicLayout _topics;
  WifiManager _wifi;
  NtpService _ntp;
  MqttService _mqtt;
  PreferenceNetworkConfigStore _store;
  NetworkRuntime _runtime;
  InboundHandler _handlers[kMaxInboundHandlers];
  size_t _handlerCount;

  /**
   * Returns the MQTT service used by sibling modules.
   *
   * @return MQTT service.
   */
  MqttService& mqtt();

  /**
   * Returns the topic tree used by sibling modules.
   *
   * @return Topic layout.
   */
  MqttTopicLayout& topics();

  /**
   * Returns the Wi-Fi state machine used by the console.
   *
   * @return Wi-Fi manager.
   */
  WifiManager& wifi();

  /**
   * Returns the NTP state machine used by the console.
   *
   * @return NTP service.
   */
  NtpService& ntp();

  /**
   * Returns the configuration coordinator used by the console.
   *
   * @return Network runtime.
   */
  NetworkRuntime& runtime();

  /**
   * Delivers one inbound payload to every registered handler.
   *
   * @param topic Received topic.
   * @param payload Payload bytes.
   * @param length Payload length.
   * @param context Network instance.
   * @return Nothing.
   */
  static void dispatch(const char* topic, const uint8_t* payload,
                       size_t length, void* context);
};

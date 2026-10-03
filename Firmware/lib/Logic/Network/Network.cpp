#include "Network.h"

// ========== Construction ==========

Network::Network(IWifi& wifi, INtpAdapter& ntp, IMqttClient& mqtt, IClock& clock,
                 IPreferenceStore& preferences, const char* defaultDeviceId,
                 const NtpConfig& ntpConfig, const NetworkTiming& timing)
  : _topics(defaultDeviceId),
    _wifi(wifi, clock,
          {"", "", timing.wifiConnectTimeoutMs, timing.wifiInitialRetryMs,
           timing.wifiMaxRetryMs, nullptr}),
    _ntp(ntp, clock, ntpConfig),
    _mqtt(mqtt, clock,
          {"watering-controller", nullptr, nullptr, _topics.subscriptions(),
           _topics.subscriptionCount(), timing.mqttInitialRetryMs,
           timing.mqttMaxRetryMs}),
    _store(preferences),
    _runtime(_store, _wifi, _mqtt, _topics),
    _handlerCount(0)
{
  (void)clock;
  for (size_t i = 0; i < kMaxInboundHandlers; ++i)
  {
    _handlers[i].callback = nullptr;
    _handlers[i].context = nullptr;
  }
  _mqtt.setMessageHandler(&Network::dispatch, this);
}


// ========== Public API ==========

/**
 * Loads the stored record over defaults, then starts Wi-Fi, MQTT, and NTP.
 *
 * @param defaults Values used for keys that are not stored.
 * @return True when at least one network key was stored.
 */
bool Network::begin(const NetworkConfig& defaults)
{
  const bool loaded = _runtime.begin(defaults);
  _ntp.begin();
  return loaded;
}

/**
 * Advances Wi-Fi, then NTP, then MQTT.
 *
 * @return Nothing.
 */
void Network::update()
{
  _wifi.update();
  _ntp.update();
  _mqtt.update(_wifi.isConnected());
}

/**
 * Publishes one payload when the broker is connected.
 *
 * @param topic Topic name.
 * @param payload Message payload.
 * @param retained Retained-message flag.
 * @return True when publication was accepted.
 */
bool Network::publish(const char* topic, const char* payload, bool retained)
{
  return _mqtt.publish(topic, payload, retained);
}

/**
 * Registers an inbound MQTT handler.
 *
 * @param callback Handler function.
 * @param context Opaque callback context.
 * @return True when the handler was stored.
 */
bool Network::addMessageHandler(MqttMessageCallback callback, void* context)
{
  if (callback == nullptr || _handlerCount >= kMaxInboundHandlers)
  {
    return false;
  }
  _handlers[_handlerCount].callback = callback;
  _handlers[_handlerCount].context = context;
  _handlerCount++;
  return true;
}

/**
 * Returns the boot warning from the last configuration load.
 *
 * @return Warning text, or an empty string.
 */
const char* Network::loadWarning() const
{
  return _store.loadWarning();
}

/**
 * Returns the active network configuration.
 *
 * @return Active configuration.
 */
const NetworkConfig& Network::config() const
{
  return _runtime.config();
}

/**
 * Persists the selected fields and applies them.
 *
 * @param config Values for the selected fields.
 * @param fields Fields to persist and apply.
 * @return True when persistence succeeds.
 */
bool Network::apply(const NetworkConfig& config,
                    const NetworkConfigFieldMask& fields)
{
  return _runtime.apply(config, fields);
}

/**
 * Reports whether a hostname or MQTT prefix can be stored.
 *
 * @param name Candidate name.
 * @return True when the name is valid.
 */
bool Network::isValidHostname(const char* name)
{
  return NetworkRuntime::isValidHostname(name);
}


// ========== Sibling access ==========

/**
 * Returns the MQTT service used by sibling modules.
 *
 * @return MQTT service.
 */
MqttService& Network::mqtt()
{
  return _mqtt;
}

/**
 * Returns the topic tree used by sibling modules.
 *
 * @return Topic layout.
 */
MqttTopicLayout& Network::topics()
{
  return _topics;
}

/**
 * Returns the Wi-Fi state machine used by the console.
 *
 * @return Wi-Fi manager.
 */
WifiManager& Network::wifi()
{
  return _wifi;
}

/**
 * Returns the NTP state machine used by the console.
 *
 * @return NTP service.
 */
NtpService& Network::ntp()
{
  return _ntp;
}

/**
 * Returns the configuration coordinator used by the console.
 *
 * @return Network runtime.
 */
NetworkRuntime& Network::runtime()
{
  return _runtime;
}

/**
 * Delivers one inbound payload to every registered handler.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @param context Network instance.
 * @return Nothing.
 */
void Network::dispatch(const char* topic, const uint8_t* payload,
                       size_t length, void* context)
{
  Network* network = static_cast<Network*>(context);
  if (network == nullptr)
  {
    return;
  }
  for (size_t i = 0; i < network->_handlerCount; ++i)
  {
    network->_handlers[i].callback(topic, payload, length,
                                   network->_handlers[i].context);
  }
}

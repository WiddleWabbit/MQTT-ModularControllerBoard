#pragma once

#include <string>

#include "INetworkConfigStore.h"
#include "MqttService.h"
#include "WifiManager.h"

/**
 * Applies one network configuration to WiFi and MQTT services.
 */
class NetworkRuntime
{
public:
  /**
   * Creates a runtime configuration coordinator.
   *
   * @param store Persistent configuration store.
   * @param wifi WiFi state machine.
   * @param mqtt MQTT state machine.
   */
  NetworkRuntime(INetworkConfigStore& store, WifiManager& wifi,
                 MqttService& mqtt);

  /**
   * Loads and applies persisted settings, or applies supplied defaults.
   *
   * @param defaults Configuration used when storage is empty.
   * @return True when persisted settings were loaded.
   */
  bool begin(const NetworkConfig& defaults);

  /**
   * Persists the selected fields and applies them to the running services.
   *
   * Unselected fields keep their active values and are left untouched in
   * storage. An empty selection succeeds without reconnecting.
   *
   * @param config Values for the selected fields.
   * @param fields Fields to persist and apply.
   * @return True when persistence succeeds.
   */
  bool apply(const NetworkConfig& config, const NetworkConfigFieldMask& fields);

  /**
   * Returns the active configuration.
   *
   * @return Active configuration.
   */
  const NetworkConfig& config() const;

private:
  INetworkConfigStore& _store;
  WifiManager& _wifi;
  MqttService& _mqtt;
  std::string _wifiSsid;
  std::string _wifiPassword;
  std::string _mqttHost;
  std::string _mqttClientId;
  std::string _mqttUsername;
  std::string _mqttPassword;
  NetworkConfig _config{};

  /**
   * Copies caller-owned strings into runtime-owned storage.
   *
   * @param source Configuration to copy.
   * @return Nothing.
   */
  void _copyConfig(const NetworkConfig& source);

  /**
   * Copies the selected fields into runtime-owned storage.
   *
   * @param source Configuration supplying the selected values.
   * @param fields Fields to copy.
   * @return Nothing.
   */
  void _assign(const NetworkConfig& source, const NetworkConfigFieldMask& fields);

  /**
   * Points the public configuration at runtime-owned strings.
   *
   * @return Nothing.
   */
  void _bind();

  /**
   * Installs the active WiFi credentials and starts a connection attempt.
   *
   * @return Nothing.
   */
  void _pushWifi();

  /**
   * Installs the active MQTT settings and starts broker management.
   *
   * @return Nothing.
   */
  void _pushMqtt();

  /**
   * Copies a possibly null C string.
   *
   * @param value Source text, or nullptr.
   * @return Owned text. Null becomes empty.
   */
  static std::string _text(const char* value);
};

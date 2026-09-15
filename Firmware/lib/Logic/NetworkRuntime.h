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
   * Applies and persists a new configuration.
   *
   * @param config New configuration.
   * @return True when persistence succeeds.
   */
  bool apply(const NetworkConfig& config);

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
};

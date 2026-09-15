#pragma once

#include <cstdint>

struct NetworkConfig
{
  const char* wifiSsid;
  const char* wifiPassword;
  const char* mqttHost;
  uint16_t mqttPort;
  const char* mqttClientId;
  const char* mqttUsername;
  const char* mqttPassword;
};

/**
 * Persists the network configuration independently of the storage technology.
 */
class INetworkConfigStore
{
public:
  virtual ~INetworkConfigStore() = default;

  /**
   * Loads the last saved configuration.
   *
   * @param config Destination configuration.
   * @return True when a complete saved configuration exists.
   */
  virtual bool load(NetworkConfig& config) = 0;

  /**
   * Saves a configuration atomically from the caller's perspective.
   *
   * @param config Configuration to persist.
   * @return True when storage accepted the values.
   */
  virtual bool save(const NetworkConfig& config) = 0;
};

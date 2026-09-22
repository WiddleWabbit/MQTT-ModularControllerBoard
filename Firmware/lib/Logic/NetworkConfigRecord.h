#pragma once

#include <cstdint>
#include <string>

#include "INetworkConfigStore.h"
#include "IPreferenceStore.h"

/**
 * NVS key names for the network configuration record.
 */
namespace NetworkConfigKeys
{
constexpr const char* wifiSsid = "wifi_ssid";
constexpr const char* wifiPassword = "wifi_password";
constexpr const char* mqttHost = "mqtt_host";
constexpr const char* mqttPort = "mqtt_port";
constexpr const char* mqttClientId = "mqtt_client";
constexpr const char* mqttUsername = "mqtt_user";
constexpr const char* mqttPassword = "mqtt_password";
}

/**
 * Owned copy of a network configuration plus an optional boot warning.
 */
struct NetworkConfigData
{
  std::string wifiSsid;
  std::string wifiPassword;
  std::string mqttHost;
  uint16_t mqttPort = 1883;
  std::string mqttClientId;
  std::string mqttUsername;
  std::string mqttPassword;
  std::string warning;
};

/**
 * Loads and saves individual network-configuration fields.
 */
class NetworkConfigRecord
{
public:
  /**
   * Loads stored fields over the supplied defaults.
   *
   * Missing keys keep the matching default. A missing client id uses the
   * default, records a warning, and stores that default. Returns false when
   * the store cannot be opened or contains no network keys.
   *
   * @param store Key-value store.
   * @param defaults Values used for keys that are not stored.
   * @param data Destination record.
   * @return True when at least one network key was stored.
   */
  static bool load(IPreferenceStore& store, const NetworkConfig& defaults,
                   NetworkConfigData& data);

  /**
   * Persists only the selected fields.
   *
   * An empty string is a successful write when the key exists afterwards.
   *
   * @param store Key-value store.
   * @param config Values for the selected fields.
   * @param fields Fields to write.
   * @return True when every selected write succeeded.
   */
  static bool save(IPreferenceStore& store, const NetworkConfig& config,
                   const NetworkConfigFieldMask& fields);
};

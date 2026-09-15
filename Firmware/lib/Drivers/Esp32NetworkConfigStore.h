#pragma once

#include <Preferences.h>
#include <string>

#include "INetworkConfigStore.h"

/**
 * Stores network settings in ESP32 NVS through Arduino Preferences.
 */
class Esp32NetworkConfigStore : public INetworkConfigStore
{
public:
  /**
   * Creates a store using the supplied NVS namespace.
   *
   * @param name Preferences namespace.
   */
  explicit Esp32NetworkConfigStore(const char* name = "network");

  bool load(NetworkConfig& config) override;
  bool save(const NetworkConfig& config) override;

private:
  Preferences _preferences;
  const char* _name;
  std::string _ssid;
  std::string _wifiPassword;
  std::string _mqttHost;
  std::string _clientId;
  std::string _username;
  std::string _mqttPassword;
};

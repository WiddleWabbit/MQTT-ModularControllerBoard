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
  const char* wifiHostname;
  bool statusReporting;
};

/**
 * Selects which network-configuration fields an operation touches.
 */
struct NetworkConfigFieldMask
{
  bool wifiSsid = false;
  bool wifiPassword = false;
  bool mqttHost = false;
  bool mqttPort = false;
  bool mqttClientId = false;
  bool mqttUsername = false;
  bool mqttPassword = false;
  bool wifiHostname = false;
  bool statusReporting = false;

  /**
   * Reports whether any field is selected.
   *
   * @return True when at least one field is selected.
   */
  bool any() const
  {
    return wifiSsid || wifiPassword || mqttHost || mqttPort || mqttClientId ||
           mqttUsername || mqttPassword || wifiHostname || statusReporting;
  }

  /**
   * Reports whether a WiFi station setting is selected.
   *
   * @return True when the SSID, password, or hostname is selected.
   */
  bool affectsWifi() const
  {
    return wifiSsid || wifiPassword || wifiHostname;
  }

  /**
   * Reports whether an MQTT setting is selected.
   *
   * @return True when any MQTT field is selected.
   */
  bool affectsMqtt() const
  {
    return mqttHost || mqttPort || mqttClientId || mqttUsername || mqttPassword;
  }

  /**
   * Returns a mask that selects every field.
   *
   * @return Mask with every field selected.
   */
  static NetworkConfigFieldMask all()
  {
    NetworkConfigFieldMask fields;
    fields.wifiSsid = true;
    fields.wifiPassword = true;
    fields.mqttHost = true;
    fields.mqttPort = true;
    fields.mqttClientId = true;
    fields.mqttUsername = true;
    fields.mqttPassword = true;
    fields.wifiHostname = true;
    fields.statusReporting = true;
    return fields;
  }
};

/**
 * Persists the network configuration independently of the storage technology.
 */
class INetworkConfigStore
{
public:
  virtual ~INetworkConfigStore() = default;

  /**
   * Loads stored fields over the values already in config.
   *
   * @param config Defaults on entry and the merged configuration on success.
   * @return True when at least one network setting was stored.
   */
  virtual bool load(NetworkConfig& config) = 0;

  /**
   * Persists only the selected fields.
   *
   * @param config Values for the selected fields.
   * @param fields Fields to write. Other stored fields stay unchanged.
   * @return True when every selected field was accepted.
   */
  virtual bool save(const NetworkConfig& config,
                    const NetworkConfigFieldMask& fields) = 0;

  /**
   * Returns a boot warning from the last load.
   *
   * @return Warning text, or an empty string when the last load was quiet.
   */
  virtual const char* loadWarning() const = 0;
};

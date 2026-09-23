#pragma once

#include <string>

#include "INetworkConfigStore.h"

/**
 * Records configuration loads and partial saves for desktop tests.
 */
class FakeNetworkConfigStore : public INetworkConfigStore
{
public:
  /**
   * Copies the seeded configuration when a record is present.
   *
   * @param config Destination configuration.
   * @return True when a seeded record exists and load is enabled.
   */
  bool load(NetworkConfig& config) override
  {
    loadCallCount++;
    if (!loadResult || !_hasRecord)
    {
      return false;
    }
    _bind();
    config = _view;
    return true;
  }

  /**
   * Copies only the selected fields into the seeded record.
   *
   * @param config Values for the selected fields.
   * @param fields Fields to update.
   * @return Configured save result.
   */
  bool save(const NetworkConfig& config,
            const NetworkConfigFieldMask& fields) override
  {
    saveCallCount++;
    lastFields = fields;
    if (!saveResult)
    {
      return false;
    }
    if (!_hasRecord)
    {
      _ssid.clear();
      _wifiPassword.clear();
      _mqttHost.clear();
      _clientId.clear();
      _username.clear();
      _mqttPassword.clear();
      _hostname.clear();
      _statusReporting = false;
      _port = 1883;
      _hasRecord = true;
    }
    if (fields.wifiSsid)
    {
      _ssid = text(config.wifiSsid);
    }
    if (fields.wifiPassword)
    {
      _wifiPassword = text(config.wifiPassword);
    }
    if (fields.mqttHost)
    {
      _mqttHost = text(config.mqttHost);
    }
    if (fields.mqttPort)
    {
      _port = config.mqttPort;
    }
    if (fields.mqttClientId)
    {
      _clientId = text(config.mqttClientId);
    }
    if (fields.mqttUsername)
    {
      _username = text(config.mqttUsername);
    }
    if (fields.mqttPassword)
    {
      _mqttPassword = text(config.mqttPassword);
    }
    if (fields.wifiHostname)
    {
      _hostname = text(config.wifiHostname);
    }
    if (fields.statusReporting)
    {
      _statusReporting = config.statusReporting;
    }
    _bind();
    return true;
  }

  /**
   * Returns the configured boot warning.
   *
   * @return Warning text, or an empty string.
   */
  const char* loadWarning() const override
  {
    return warning.c_str();
  }

  /**
   * Seeds a complete stored record and enables loading.
   *
   * @param config Record to copy.
   * @return Nothing.
   */
  void seed(const NetworkConfig& config)
  {
    _hasRecord = true;
    loadResult = true;
    _ssid = text(config.wifiSsid);
    _wifiPassword = text(config.wifiPassword);
    _mqttHost = text(config.mqttHost);
    _port = config.mqttPort;
    _clientId = text(config.mqttClientId);
    _username = text(config.mqttUsername);
    _mqttPassword = text(config.mqttPassword);
    _hostname = text(config.wifiHostname);
    _statusReporting = config.statusReporting;
    _bind();
  }

  NetworkConfigFieldMask lastFields{};
  std::string warning;
  bool loadResult = false;
  bool saveResult = true;
  int loadCallCount = 0;
  int saveCallCount = 0;

  /**
   * Returns the stored WiFi SSID.
   *
   * @return Stored SSID.
   */
  const std::string& wifiSsid() const
  {
    return _ssid;
  }

  /**
   * Returns the stored WiFi password.
   *
   * @return Stored password.
   */
  const std::string& wifiPassword() const
  {
    return _wifiPassword;
  }

  /**
   * Returns the stored MQTT host.
   *
   * @return Stored host.
   */
  const std::string& mqttHost() const
  {
    return _mqttHost;
  }

  /**
   * Returns the stored station hostname.
   *
   * @return Stored hostname.
   */
  const std::string& wifiHostname() const
  {
    return _hostname;
  }

  /**
   * Returns the stored periodic-status flag.
   *
   * @return True when stored reporting is enabled.
   */
  bool statusReporting() const
  {
    return _statusReporting;
  }

private:
  /**
   * Copies a possibly null C string.
   *
   * @param value Source text, or nullptr.
   * @return Owned text. Null becomes empty.
   */
  static std::string text(const char* value)
  {
    return value == nullptr ? std::string() : std::string(value);
  }

  /**
   * Points the public view at the owned field storage.
   *
   * @return Nothing.
   */
  void _bind()
  {
    _view = {_ssid.c_str(), _wifiPassword.c_str(), _mqttHost.c_str(), _port,
             _clientId.c_str(),
             _username.empty() ? nullptr : _username.c_str(),
             _mqttPassword.empty() ? nullptr : _mqttPassword.c_str(),
             _hostname.c_str(), _statusReporting};
  }

  bool _hasRecord = false;
  std::string _ssid;
  std::string _wifiPassword;
  std::string _mqttHost;
  std::string _clientId;
  std::string _username;
  std::string _mqttPassword;
  std::string _hostname;
  bool _statusReporting = false;
  uint16_t _port = 1883;
  NetworkConfig _view{};
};

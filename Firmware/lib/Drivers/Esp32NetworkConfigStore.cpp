#include "Esp32NetworkConfigStore.h"

Esp32NetworkConfigStore::Esp32NetworkConfigStore(const char* name)
  : _name(name)
{
}

bool Esp32NetworkConfigStore::load(NetworkConfig& config)
{
  if (!_preferences.begin(_name, true) ||
      !_preferences.isKey("wifi_ssid") ||
      !_preferences.isKey("mqtt_host"))
  {
    _preferences.end();
    return false;
  }

  _ssid = _preferences.getString("wifi_ssid", "").c_str();
  _wifiPassword = _preferences.getString("wifi_password", "").c_str();
  _mqttHost = _preferences.getString("mqtt_host", "").c_str();
  _clientId = _preferences.getString("mqtt_client", "watering-controller").c_str();
  _username = _preferences.getString("mqtt_user", "").c_str();
  _mqttPassword = _preferences.getString("mqtt_password", "").c_str();
  config = {_ssid.c_str(), _wifiPassword.c_str(), _mqttHost.c_str(),
            _preferences.getUShort("mqtt_port", 1883), _clientId.c_str(),
            _username.empty() ? nullptr : _username.c_str(),
            _mqttPassword.empty() ? nullptr : _mqttPassword.c_str()};
  _preferences.end();
  return true;
}

bool Esp32NetworkConfigStore::save(const NetworkConfig& config)
{
  if (!_preferences.begin(_name, false))
  {
    return false;
  }
  const bool success =
    _preferences.putString("wifi_ssid", config.wifiSsid == nullptr ? "" : config.wifiSsid) > 0 &&
    _preferences.putString("wifi_password",
                           config.wifiPassword == nullptr ? "" : config.wifiPassword) > 0 &&
    _preferences.putString("mqtt_host", config.mqttHost == nullptr ? "" : config.mqttHost) > 0 &&
    _preferences.putUShort("mqtt_port", config.mqttPort) > 0 &&
    _preferences.putString("mqtt_client",
                           config.mqttClientId == nullptr ? "" : config.mqttClientId) > 0;
  _preferences.putString("mqtt_user", config.mqttUsername == nullptr ? "" : config.mqttUsername);
  _preferences.putString("mqtt_password",
                         config.mqttPassword == nullptr ? "" : config.mqttPassword);
  _preferences.end();
  return success;
}

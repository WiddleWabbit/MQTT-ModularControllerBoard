#include "PreferenceNetworkConfigStore.h"

#include "NetworkConfigRecord.h"

PreferenceNetworkConfigStore::PreferenceNetworkConfigStore(IPreferenceStore& store)
  : _store(store)
{
}

bool PreferenceNetworkConfigStore::load(NetworkConfig& config)
{
  NetworkConfigData data;
  if (!NetworkConfigRecord::load(_store, config, data))
  {
    _warning.clear();
    return false;
  }

  _ssid = data.wifiSsid;
  _wifiPassword = data.wifiPassword;
  _mqttHost = data.mqttHost;
  _clientId = data.mqttClientId;
  _username = data.mqttUsername;
  _mqttPassword = data.mqttPassword;
  _warning = data.warning;
  config = {_ssid.c_str(), _wifiPassword.c_str(), _mqttHost.c_str(),
            data.mqttPort, _clientId.c_str(),
            _username.empty() ? nullptr : _username.c_str(),
            _mqttPassword.empty() ? nullptr : _mqttPassword.c_str()};
  return true;
}

bool PreferenceNetworkConfigStore::save(const NetworkConfig& config,
                                        const NetworkConfigFieldMask& fields)
{
  return NetworkConfigRecord::save(_store, config, fields);
}

const char* PreferenceNetworkConfigStore::loadWarning() const
{
  return _warning.c_str();
}

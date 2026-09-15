#include "NetworkRuntime.h"

NetworkRuntime::NetworkRuntime(INetworkConfigStore& store, WifiManager& wifi,
                               MqttService& mqtt)
  : _store(store), _wifi(wifi), _mqtt(mqtt)
{
}

bool NetworkRuntime::begin(const NetworkConfig& defaults)
{
  NetworkConfig loadedConfig = defaults;
  const bool loaded = _store.load(loadedConfig);
  _copyConfig(loadedConfig);
  _wifi.reconfigure({_config.wifiSsid, _config.wifiPassword,
                     _wifi.config().connectTimeoutMs,
                     _wifi.config().initialRetryDelayMs,
                     _wifi.config().maxRetryDelayMs});
  MqttConfig mqttConfig = _mqtt.config();
  _mqtt.setBroker(_config.mqttHost, _config.mqttPort);
  mqttConfig.clientId = _config.mqttClientId;
  mqttConfig.username = _config.mqttUsername;
  mqttConfig.password = _config.mqttPassword;
  _mqtt.reconfigure(mqttConfig);
  _wifi.begin();
  _mqtt.begin();
  return loaded;
}

bool NetworkRuntime::apply(const NetworkConfig& config)
{
  if (!_store.save(config))
  {
    return false;
  }

  _copyConfig(config);
  _wifi.reconfigure({_config.wifiSsid, _config.wifiPassword,
                     _wifi.config().connectTimeoutMs,
                     _wifi.config().initialRetryDelayMs,
                     _wifi.config().maxRetryDelayMs});
  MqttConfig mqttConfig = _mqtt.config();
  _mqtt.setBroker(_config.mqttHost, _config.mqttPort);
  mqttConfig.clientId = _config.mqttClientId;
  mqttConfig.username = _config.mqttUsername;
  mqttConfig.password = _config.mqttPassword;
  _mqtt.reconfigure(mqttConfig);
  _wifi.begin();
  _mqtt.begin();
  return true;
}

const NetworkConfig& NetworkRuntime::config() const
{
  return _config;
}

void NetworkRuntime::_copyConfig(const NetworkConfig& source)
{
  _wifiSsid = source.wifiSsid == nullptr ? "" : source.wifiSsid;
  _wifiPassword = source.wifiPassword == nullptr ? "" : source.wifiPassword;
  _mqttHost = source.mqttHost == nullptr ? "" : source.mqttHost;
  _mqttClientId = source.mqttClientId == nullptr ? "" : source.mqttClientId;
  _mqttUsername = source.mqttUsername == nullptr ? "" : source.mqttUsername;
  _mqttPassword = source.mqttPassword == nullptr ? "" : source.mqttPassword;

  _config.wifiSsid = _wifiSsid.c_str();
  _config.wifiPassword = _wifiPassword.c_str();
  _config.mqttHost = _mqttHost.c_str();
  _config.mqttPort = source.mqttPort;
  _config.mqttClientId = _mqttClientId.c_str();
  _config.mqttUsername =
    _mqttUsername.empty() ? nullptr : _mqttUsername.c_str();
  _config.mqttPassword =
    _mqttPassword.empty() ? nullptr : _mqttPassword.c_str();
}

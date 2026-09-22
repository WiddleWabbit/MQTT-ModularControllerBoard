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
  _pushWifi();
  _pushMqtt();
  return loaded;
}

bool NetworkRuntime::apply(const NetworkConfig& config,
                           const NetworkConfigFieldMask& fields)
{
  if (!fields.any())
  {
    return true;
  }
  if (!_store.save(config, fields))
  {
    return false;
  }

  _assign(config, fields);
  if (fields.affectsWifi())
  {
    _pushWifi();
  }
  if (fields.affectsMqtt())
  {
    _pushMqtt();
  }
  return true;
}

const NetworkConfig& NetworkRuntime::config() const
{
  return _config;
}

void NetworkRuntime::_copyConfig(const NetworkConfig& source)
{
  _assign(source, NetworkConfigFieldMask::all());
}

void NetworkRuntime::_assign(const NetworkConfig& source,
                             const NetworkConfigFieldMask& fields)
{
  if (fields.wifiSsid)
  {
    _wifiSsid = _text(source.wifiSsid);
  }
  if (fields.wifiPassword)
  {
    _wifiPassword = _text(source.wifiPassword);
  }
  if (fields.mqttHost)
  {
    _mqttHost = _text(source.mqttHost);
  }
  if (fields.mqttPort)
  {
    _config.mqttPort = source.mqttPort;
  }
  if (fields.mqttClientId)
  {
    _mqttClientId = _text(source.mqttClientId);
  }
  if (fields.mqttUsername)
  {
    _mqttUsername = _text(source.mqttUsername);
  }
  if (fields.mqttPassword)
  {
    _mqttPassword = _text(source.mqttPassword);
  }
  _bind();
}

void NetworkRuntime::_bind()
{
  _config.wifiSsid = _wifiSsid.c_str();
  _config.wifiPassword = _wifiPassword.c_str();
  _config.mqttHost = _mqttHost.c_str();
  _config.mqttClientId = _mqttClientId.c_str();
  _config.mqttUsername =
    _mqttUsername.empty() ? nullptr : _mqttUsername.c_str();
  _config.mqttPassword =
    _mqttPassword.empty() ? nullptr : _mqttPassword.c_str();
}

void NetworkRuntime::_pushWifi()
{
  _wifi.reconfigure({_config.wifiSsid, _config.wifiPassword,
                     _wifi.config().connectTimeoutMs,
                     _wifi.config().initialRetryDelayMs,
                     _wifi.config().maxRetryDelayMs});
  _wifi.begin();
}

void NetworkRuntime::_pushMqtt()
{
  MqttConfig mqttConfig = _mqtt.config();
  _mqtt.setBroker(_config.mqttHost, _config.mqttPort);
  mqttConfig.clientId = _config.mqttClientId;
  mqttConfig.username = _config.mqttUsername;
  mqttConfig.password = _config.mqttPassword;
  _mqtt.reconfigure(mqttConfig);
  _mqtt.begin();
}

std::string NetworkRuntime::_text(const char* value)
{
  return value == nullptr ? std::string() : std::string(value);
}

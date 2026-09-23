#include "NetworkRuntime.h"

#include <cstddef>
#include <cstring>

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
  if (fields.wifiHostname && !isValidHostname(config.wifiHostname))
  {
    return false;
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

/**
 * Reports whether a hostname can be stored and advertised.
 *
 * A valid name is 1–31 characters, uses letters, digits, and hyphens, and
 * starts and ends with a letter or digit.
 *
 * @param hostname Candidate hostname.
 * @return True when the hostname is valid.
 */
bool NetworkRuntime::isValidHostname(const char* hostname)
{
  if (hostname == nullptr || hostname[0] == '\0')
  {
    return false;
  }

  const size_t length = std::strlen(hostname);
  if (length > 31)
  {
    return false;
  }

  for (size_t index = 0; index < length; ++index)
  {
    const char character = hostname[index];
    const bool digit = character >= '0' && character <= '9';
    const bool upper = character >= 'A' && character <= 'Z';
    const bool lower = character >= 'a' && character <= 'z';
    const bool hyphen = character == '-';
    if (!digit && !upper && !lower && !hyphen)
    {
      return false;
    }
    const bool boundary = index == 0 || index + 1 == length;
    if (boundary && hyphen)
    {
      return false;
    }
  }
  return true;
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
  if (fields.wifiHostname)
  {
    _wifiHostname = _text(source.wifiHostname);
  }
  if (fields.statusReporting)
  {
    _statusReporting = source.statusReporting;
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
  _config.wifiHostname = _wifiHostname.c_str();
  _config.statusReporting = _statusReporting;
}

void NetworkRuntime::_pushWifi()
{
  _wifi.reconfigure({_config.wifiSsid, _config.wifiPassword,
                     _wifi.config().connectTimeoutMs,
                     _wifi.config().initialRetryDelayMs,
                     _wifi.config().maxRetryDelayMs, _config.wifiHostname});
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

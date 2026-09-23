#include "NetworkConfigRecord.h"

#include <cstring>

namespace
{
constexpr const char* defaultMqttClientId = "watering-controller";

/**
 * Copies a possibly null C string.
 *
 * @param value Source text, or nullptr.
 * @return Owned text. Null becomes empty.
 */
std::string copyText(const char* value)
{
  return value == nullptr ? std::string() : std::string(value);
}

/**
 * Reads a string key, or the fallback when the key is absent.
 *
 * @param store Open key-value store.
 * @param key Key name.
 * @param fallback Value used when the key is absent.
 * @return Stored text or the fallback.
 */
std::string readOptional(IPreferenceStore& store, const char* key,
                         const char* fallback)
{
  if (!store.contains(key))
  {
    return copyText(fallback);
  }
  return store.readString(key);
}

/**
 * Writes a string, treating an empty value as success only when it is stored.
 *
 * @param store Open key-value store.
 * @param key Key name.
 * @param value Text to store. Null is stored as empty.
 * @return True when the key holds the requested text.
 */
bool writeText(IPreferenceStore& store, const char* key, const char* value)
{
  const char* text = value == nullptr ? "" : value;
  const size_t written = store.writeString(key, text);
  if (written != std::strlen(text))
  {
    return false;
  }
  return text[0] != '\0' || store.contains(key);
}

/**
 * Reports whether any network key is already stored.
 *
 * @param store Open key-value store.
 * @return True when at least one network key exists.
 */
bool hasNetworkKey(IPreferenceStore& store)
{
  return store.contains(NetworkConfigKeys::wifiSsid) ||
         store.contains(NetworkConfigKeys::wifiPassword) ||
         store.contains(NetworkConfigKeys::mqttHost) ||
         store.contains(NetworkConfigKeys::mqttPort) ||
         store.contains(NetworkConfigKeys::mqttClientId) ||
         store.contains(NetworkConfigKeys::mqttUsername) ||
         store.contains(NetworkConfigKeys::mqttPassword) ||
         store.contains(NetworkConfigKeys::wifiHostname) ||
         store.contains(NetworkConfigKeys::statusReport);
}
}


// ========== Public API ==========

bool NetworkConfigRecord::load(IPreferenceStore& store,
                               const NetworkConfig& defaults,
                               NetworkConfigData& data)
{
  if (!store.open(true))
  {
    return false;
  }
  if (!hasNetworkKey(store))
  {
    store.close();
    return false;
  }

  const char* clientFallback = defaults.mqttClientId;
  if (clientFallback == nullptr || clientFallback[0] == '\0')
  {
    clientFallback = defaultMqttClientId;
  }

  const bool clientMissing = !store.contains(NetworkConfigKeys::mqttClientId);
  data.wifiSsid = readOptional(store, NetworkConfigKeys::wifiSsid,
                               defaults.wifiSsid);
  data.wifiPassword = readOptional(store, NetworkConfigKeys::wifiPassword,
                                   defaults.wifiPassword);
  data.mqttHost = readOptional(store, NetworkConfigKeys::mqttHost,
                               defaults.mqttHost);
  data.mqttPort = store.contains(NetworkConfigKeys::mqttPort)
                     ? store.readUShort(NetworkConfigKeys::mqttPort,
                                        defaults.mqttPort)
                     : defaults.mqttPort;
  data.mqttClientId = clientMissing
                        ? std::string(clientFallback)
                        : store.readString(NetworkConfigKeys::mqttClientId);
  data.mqttUsername = readOptional(store, NetworkConfigKeys::mqttUsername,
                                   defaults.mqttUsername);
  data.mqttPassword = readOptional(store, NetworkConfigKeys::mqttPassword,
                                   defaults.mqttPassword);
  data.wifiHostname = readOptional(store, NetworkConfigKeys::wifiHostname,
                                   defaults.wifiHostname);
  data.statusReporting =
    store.contains(NetworkConfigKeys::statusReport)
      ? store.readUShort(NetworkConfigKeys::statusReport,
                         defaults.statusReporting ? 1 : 0) != 0
      : defaults.statusReporting;
  data.warning.clear();
  store.close();

  if (clientMissing)
  {
    data.warning = std::string("Config: mqtt client id is not stored; using ") +
                   data.mqttClientId;
    if (store.open(false))
    {
      writeText(store, NetworkConfigKeys::mqttClientId,
                data.mqttClientId.c_str());
      store.close();
    }
  }
  return true;
}

bool NetworkConfigRecord::save(IPreferenceStore& store,
                               const NetworkConfig& config,
                               const NetworkConfigFieldMask& fields)
{
  if (!fields.any())
  {
    return true;
  }
  if (!store.open(false))
  {
    return false;
  }

  bool succeeded = true;
  if (fields.wifiSsid)
  {
    succeeded = writeText(store, NetworkConfigKeys::wifiSsid, config.wifiSsid) &&
                succeeded;
  }
  if (fields.wifiPassword)
  {
    succeeded = writeText(store, NetworkConfigKeys::wifiPassword,
                          config.wifiPassword) &&
                succeeded;
  }
  if (fields.mqttHost)
  {
    succeeded = writeText(store, NetworkConfigKeys::mqttHost, config.mqttHost) &&
                succeeded;
  }
  if (fields.mqttPort)
  {
    succeeded = (store.writeUShort(NetworkConfigKeys::mqttPort,
                                   config.mqttPort) == 2) &&
                succeeded;
  }
  if (fields.mqttClientId)
  {
    succeeded = writeText(store, NetworkConfigKeys::mqttClientId,
                          config.mqttClientId) &&
                succeeded;
  }
  if (fields.mqttUsername)
  {
    succeeded = writeText(store, NetworkConfigKeys::mqttUsername,
                          config.mqttUsername) &&
                succeeded;
  }
  if (fields.mqttPassword)
  {
    succeeded = writeText(store, NetworkConfigKeys::mqttPassword,
                          config.mqttPassword) &&
                succeeded;
  }
  if (fields.wifiHostname)
  {
    succeeded = writeText(store, NetworkConfigKeys::wifiHostname,
                          config.wifiHostname) &&
                succeeded;
  }
  if (fields.statusReporting)
  {
    succeeded = (store.writeUShort(NetworkConfigKeys::statusReport,
                                   config.statusReporting ? 1 : 0) == 2) &&
                succeeded;
  }
  store.close();
  return succeeded;
}

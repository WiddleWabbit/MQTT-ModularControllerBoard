#pragma once

#include <string>

#include "INetworkConfigStore.h"
#include "IPreferenceStore.h"

/**
 * Persists network settings one field at a time through a key-value store.
 */
class PreferenceNetworkConfigStore : public INetworkConfigStore
{
public:
  /**
   * Creates a store that keeps loaded strings alive for the returned pointers.
   *
   * @param store Key-value store. It must outlive this object.
   */
  explicit PreferenceNetworkConfigStore(IPreferenceStore& store);

  bool load(NetworkConfig& config) override;
  bool save(const NetworkConfig& config,
            const NetworkConfigFieldMask& fields) override;
  const char* loadWarning() const override;

private:
  IPreferenceStore& _store;
  std::string _ssid;
  std::string _wifiPassword;
  std::string _mqttHost;
  std::string _clientId;
  std::string _username;
  std::string _mqttPassword;
  std::string _warning;
};

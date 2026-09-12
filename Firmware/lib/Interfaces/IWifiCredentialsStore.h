#pragma once

#include <cstddef>

struct WifiCredentials {
  static constexpr size_t maximumSsidLength = 32;
  static constexpr size_t maximumPasswordLength = 63;

  char ssid[maximumSsidLength + 1] = {};
  char password[maximumPasswordLength + 1] = {};
};

class IWifiCredentialsStore {
public:
  /**
   * Releases the interface without owning the concrete storage driver.
   *
   * @return Nothing.
   */
  virtual ~IWifiCredentialsStore() = default;

  /**
   * Opens the persistent credentials store.
   *
   * @return True when the store is ready for use.
   */
  virtual bool begin() = 0;

  /**
   * Loads stored credentials.
   *
   * @param credentials Destination credentials structure.
   * @return True when a non-empty SSID is available.
   */
  virtual bool load(WifiCredentials& credentials) = 0;

  /**
   * Persists credentials for use on the next boot.
   *
   * @param credentials Credentials to persist.
   * @return True when the save succeeds.
   */
  virtual bool save(const WifiCredentials& credentials) = 0;

  /**
   * Removes all persisted credentials.
   *
   * @return Nothing.
   */
  virtual void clear() = 0;
};

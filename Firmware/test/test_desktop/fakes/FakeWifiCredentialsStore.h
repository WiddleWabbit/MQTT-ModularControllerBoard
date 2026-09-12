#pragma once

#include <cstring>

#include "IWifiCredentialsStore.h"

class FakeWifiCredentialsStore : public IWifiCredentialsStore {
public:
  /**
   * Records initialization of the simulated credential store.
   *
   * @return True when the fake is configured to initialize successfully.
   */
  bool begin() override
  {
    beginCallCount++;
    return beginResult;
  }

  /**
   * Returns the configured credentials when available.
   *
   * @param credentials Destination credentials structure.
   * @return True when credentials are configured and available.
   */
  bool load(WifiCredentials& credentials) override
  {
    loadCallCount++;
    if (!hasStoredCredentials) {
      return false;
    }

    credentials = storedCredentials;
    return true;
  }

  /**
   * Stores credentials when saving is enabled.
   *
   * @param credentials Credentials supplied by the system under test.
   * @return True when the simulated save succeeds.
   */
  bool save(const WifiCredentials& credentials) override
  {
    saveCallCount++;
    lastSavedCredentials = credentials;
    if (!saveResult) {
      return false;
    }

    storedCredentials = credentials;
    hasStoredCredentials = true;
    return true;
  }

  /**
   * Clears all simulated credentials.
   *
   * @return Nothing.
   */
  void clear() override
  {
    clearCallCount++;
    hasStoredCredentials = false;
    storedCredentials = {};
  }

  /**
   * Configures stored credentials for a test.
   *
   * @param ssid Stored network name.
   * @param password Stored network password.
   * @return Nothing.
   */
  void setStoredCredentials(const char* ssid, const char* password)
  {
    storedCredentials = {};
    if (ssid != nullptr) {
      std::strncpy(storedCredentials.ssid, ssid,
                   WifiCredentials::maximumSsidLength);
      storedCredentials.ssid[WifiCredentials::maximumSsidLength] = '\0';
    }
    if (password != nullptr) {
      std::strncpy(storedCredentials.password, password,
                   WifiCredentials::maximumPasswordLength);
      storedCredentials.password[WifiCredentials::maximumPasswordLength] =
          '\0';
    }
    hasStoredCredentials = storedCredentials.ssid[0] != '\0';
  }

  bool beginResult = true;
  bool saveResult = true;
  bool hasStoredCredentials = false;
  WifiCredentials storedCredentials = {};
  WifiCredentials lastSavedCredentials = {};
  unsigned int beginCallCount = 0;
  unsigned int loadCallCount = 0;
  unsigned int saveCallCount = 0;
  unsigned int clearCallCount = 0;
};

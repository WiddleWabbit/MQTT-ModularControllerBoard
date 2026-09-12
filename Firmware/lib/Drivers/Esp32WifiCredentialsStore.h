#pragma once

#include "IWifiCredentialsStore.h"

#ifdef ARDUINO
#include <Preferences.h>
#endif

class Esp32WifiCredentialsStore : public IWifiCredentialsStore {
public:
  /**
   * Creates a Preferences-backed credential store.
   *
   * @param name Preferences namespace, limited by the ESP32 Preferences API.
   */
  explicit Esp32WifiCredentialsStore(const char* name = "wifi");

  /**
   * Opens the Preferences namespace for read/write access.
   *
   * @return True when the namespace opens successfully.
   */
  bool begin() override;

  /**
   * Loads the saved SSID and password.
   *
   * @param credentials Destination credentials structure.
   * @return True when a saved SSID exists.
   */
  bool load(WifiCredentials& credentials) override;

  /**
   * Saves the SSID and password to non-volatile storage.
   *
   * @param credentials Credentials to save.
   * @return True when both values are written.
   */
  bool save(const WifiCredentials& credentials) override;

  /**
   * Deletes all values in the WiFi Preferences namespace.
   *
   * @return Nothing.
   */
  void clear() override;

private:
  const char* _name;
#ifdef ARDUINO
  Preferences* _preferences;
#endif
};

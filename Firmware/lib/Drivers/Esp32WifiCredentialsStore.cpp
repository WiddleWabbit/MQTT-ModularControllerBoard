#include "Esp32WifiCredentialsStore.h"

#include <cstring>

#ifdef ARDUINO

#include <Preferences.h>

namespace {
Preferences preferences;
}

Esp32WifiCredentialsStore::Esp32WifiCredentialsStore(const char* name)
    : _name(name == nullptr ? "wifi" : name), _preferences(&preferences)
{
}

bool Esp32WifiCredentialsStore::begin()
{
  return _preferences->begin(_name, false);
}

bool Esp32WifiCredentialsStore::load(WifiCredentials& credentials)
{
  credentials = {};
  const String ssid = _preferences->getString("ssid", "");
  if (ssid.isEmpty()) {
    return false;
  }

  const String password = _preferences->getString("pass", "");
  std::strncpy(credentials.ssid, ssid.c_str(),
               WifiCredentials::maximumSsidLength);
  std::strncpy(credentials.password, password.c_str(),
               WifiCredentials::maximumPasswordLength);
  credentials.ssid[WifiCredentials::maximumSsidLength] = '\0';
  credentials.password[WifiCredentials::maximumPasswordLength] = '\0';
  return true;
}

bool Esp32WifiCredentialsStore::save(const WifiCredentials& credentials)
{
  const size_t ssidBytes = _preferences->putString("ssid", credentials.ssid);
  const size_t passwordBytes =
      _preferences->putString("pass", credentials.password);
  if (ssidBytes == 0 ||
      (passwordBytes == 0 && credentials.password[0] != '\0')) {
    return false;
  }

  const String storedSsid = _preferences->getString("ssid", "");
  const String storedPassword = _preferences->getString("pass", "");
  return storedSsid == credentials.ssid &&
         storedPassword == credentials.password;
}

void Esp32WifiCredentialsStore::clear()
{
  _preferences->clear();
}

#else

Esp32WifiCredentialsStore::Esp32WifiCredentialsStore(const char* name)
    : _name(name == nullptr ? "wifi" : name)
{
}

bool Esp32WifiCredentialsStore::begin()
{
  return false;
}

bool Esp32WifiCredentialsStore::load(WifiCredentials&)
{
  return false;
}

bool Esp32WifiCredentialsStore::save(const WifiCredentials&)
{
  return false;
}

void Esp32WifiCredentialsStore::clear()
{
}

#endif

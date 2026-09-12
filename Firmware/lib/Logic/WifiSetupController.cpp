#include "WifiSetupController.h"

#include <cctype>
#include <cstring>

// ========== Construction ==========

WifiSetupController::WifiSetupController(IWifiScanner& scanner,
                                         IWifiCredentialsStore& store,
                                         size_t maximumVisibleNetworks)
    : _scanner(scanner),
      _store(store),
      _maximumVisibleNetworks(
          maximumVisibleNetworks > maximumStoredNetworks
              ? maximumStoredNetworks
              : maximumVisibleNetworks)
{
}


// ========== Workflow ==========

void WifiSetupController::begin()
{
  _state = WifiSetupState::Idle;
  _credentials = {};
  _networkCount = 0;
  _scanFailed = false;
  _saveFailed = false;

  if (_store.begin() && _store.load(_credentials)) {
    _state = WifiSetupState::StationReady;
    return;
  }

  WifiNetworkInfo scannedNetworks[maximumStoredNetworks] = {};
  const int scanResult =
      _scanner.scanNetworks(scannedNetworks, _maximumVisibleNetworks);
  if (scanResult < 0) {
    _scanFailed = true;
  } else {
    const size_t resultCount =
        static_cast<size_t>(scanResult) > _maximumVisibleNetworks
            ? _maximumVisibleNetworks
            : static_cast<size_t>(scanResult);
    for (size_t index = 0; index < resultCount; ++index) {
      addNetwork(scannedNetworks[index]);
    }
  }

  _state = WifiSetupState::PortalReady;
}

bool WifiSetupController::submitCredentials(const char* selectedSsid,
                                            const char* manualSsid,
                                            const char* password)
{
  if (_state != WifiSetupState::PortalReady) {
    _saveFailed = false;
    return false;
  }

  const char* requestedSsid =
      hasVisibleText(manualSsid) ? manualSsid : selectedSsid;
  if (!hasVisibleText(requestedSsid) ||
      password == nullptr ||
      std::strlen(requestedSsid) > WifiCredentials::maximumSsidLength ||
      std::strlen(password) > WifiCredentials::maximumPasswordLength) {
    _saveFailed = false;
    return false;
  }

  WifiCredentials newCredentials = {};
  copyText(newCredentials.ssid, sizeof(newCredentials.ssid), requestedSsid);
  copyText(newCredentials.password, sizeof(newCredentials.password), password);

  if (!_store.save(newCredentials)) {
    _saveFailed = true;
    return false;
  }

  _credentials = newCredentials;
  _saveFailed = false;
  _state = WifiSetupState::StationReady;
  return true;
}

void WifiSetupController::clearCredentials()
{
  _store.clear();
  _credentials = {};
  _state = WifiSetupState::Idle;
}


// ========== Status ==========

WifiSetupState WifiSetupController::state() const
{
  return _state;
}

const WifiCredentials& WifiSetupController::credentials() const
{
  return _credentials;
}

size_t WifiSetupController::networkCount() const
{
  return _networkCount;
}

const WifiNetworkInfo& WifiSetupController::networkAt(size_t index) const
{
  static const WifiNetworkInfo emptyNetwork = {};
  return index < _networkCount ? _networks[index] : emptyNetwork;
}

bool WifiSetupController::scanFailed() const
{
  return _scanFailed;
}

bool WifiSetupController::saveFailed() const
{
  return _saveFailed;
}


// ========== Helpers ==========

void WifiSetupController::addNetwork(const WifiNetworkInfo& network)
{
  if (network.ssid[0] == '\0') {
    return;
  }

  for (size_t index = 0; index < _networkCount; ++index) {
    if (std::strcmp(_networks[index].ssid, network.ssid) == 0) {
      if (network.rssi > _networks[index].rssi) {
        _networks[index] = network;
      }
      return;
    }
  }

  if (_networkCount >= _maximumVisibleNetworks ||
      _networkCount >= maximumStoredNetworks) {
    return;
  }

  _networks[_networkCount] = network;
  _networkCount++;
}

void WifiSetupController::copyText(char* destination, size_t destinationSize,
                                   const char* source)
{
  if (destinationSize == 0) {
    return;
  }

  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }

  std::strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

bool WifiSetupController::hasVisibleText(const char* text)
{
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (!std::isspace(static_cast<unsigned char>(*cursor))) {
      return true;
    }
  }

  return false;
}

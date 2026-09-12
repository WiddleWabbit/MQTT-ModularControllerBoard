#pragma once

#include <cstddef>

#include "IWifiCredentialsStore.h"
#include "IWifiScanner.h"

enum class WifiSetupState {
  Idle,
  StationReady,
  PortalReady
};

class WifiSetupController {
public:
  static constexpr size_t maximumStoredNetworks = 32;

  /**
   * Creates the credential setup workflow.
   *
   * @param scanner Network scanner used when no credentials are stored.
   * @param store Persistent credential store.
   * @param maximumVisibleNetworks Maximum number of networks exposed to users.
   */
  WifiSetupController(IWifiScanner& scanner, IWifiCredentialsStore& store,
                      size_t maximumVisibleNetworks =
                          maximumStoredNetworks);

  /**
   * Loads credentials or scans networks for the captive portal.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Selects or manually enters an SSID and saves the resulting credentials.
   *
   * @param selectedSsid SSID selected from the scan list.
   * @param manualSsid Optional manually entered SSID; takes precedence.
   * @param password Network password, which may be empty.
   * @return True when valid credentials were saved.
   */
  bool submitCredentials(const char* selectedSsid, const char* manualSsid,
                         const char* password);

  /**
   * Clears persisted credentials and resets the workflow.
   *
   * @return Nothing.
   */
  void clearCredentials();

  /**
   * Reports the current setup state.
   *
   * @return Current state.
   */
  WifiSetupState state() const;

  /**
   * Reports the credentials loaded or most recently saved.
   *
   * @return Current credentials.
   */
  const WifiCredentials& credentials() const;

  /**
   * Reports the number of usable scanned networks.
   *
   * @return Number of networks available to the portal.
   */
  size_t networkCount() const;

  /**
   * Returns a scanned network by index.
   *
   * @param index Zero-based network index.
   * @return Network information, or an empty value when out of range.
   */
  const WifiNetworkInfo& networkAt(size_t index) const;

  /**
   * Reports whether the most recent scan failed.
   *
   * @return True when scan failure left the portal in manual-entry mode.
   */
  bool scanFailed() const;

  /**
   * Reports whether the most recent save failed.
   *
   * @return True after a persistence failure.
   */
  bool saveFailed() const;

private:
  /**
   * Adds a scan result unless it is hidden, duplicated, or out of capacity.
   *
   * @param network Candidate scan result.
   * @return Nothing.
   */
  void addNetwork(const WifiNetworkInfo& network);

  /**
   * Copies a bounded C string into a fixed-size destination.
   *
   * @param destination Destination buffer.
   * @param destinationSize Destination capacity.
   * @param source Source string.
   * @return Nothing.
   */
  static void copyText(char* destination, size_t destinationSize,
                       const char* source);

  /**
   * Checks whether text contains at least one non-whitespace character.
   *
   * @param text Text to inspect.
   * @return True when the text is usable as an SSID.
   */
  static bool hasVisibleText(const char* text);

  IWifiScanner& _scanner;
  IWifiCredentialsStore& _store;
  size_t _maximumVisibleNetworks;
  WifiSetupState _state = WifiSetupState::Idle;
  WifiCredentials _credentials = {};
  WifiNetworkInfo _networks[maximumStoredNetworks] = {};
  size_t _networkCount = 0;
  bool _scanFailed = false;
  bool _saveFailed = false;
};

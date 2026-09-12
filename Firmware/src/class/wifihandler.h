#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "IClock.h"
#include "ISystemControl.h"
#include "IWifiStation.h"
#include "IWifiCredentialsStore.h"
#include "IWifiScanner.h"
#include "WifiConnectionManager.h"
#include "WifiSetupController.h"
#include "WifiSetupPortalView.h"

class WiFiHandler {
public:
  /**
   * Creates the top-level WiFi station and captive-portal coordinator.
   *
   * @param hostname   Used as hostname in STA mode and as the AP SSID for the captive portal.
   * @param apPassword Optional password for the captive-portal AP (nullptr or "" = open network).
   * @param wifi Optional station driver; the ESP32 driver is used when omitted.
   * @param clock Optional monotonic clock; the ESP32 clock is used when omitted.
   * @param system Optional system control; the ESP32 control is used when omitted.
   * @param scanner Optional network scanner; the ESP32 scanner is used when omitted.
   * @param store Optional credential store; ESP32 Preferences are used when omitted.
   */
  WiFiHandler(const char* hostname = "MQTTController-Setup",
             const char* apPassword = nullptr,
             IWifiStation* wifi = nullptr,
             IClock* clock = nullptr,
             ISystemControl* system = nullptr,
             IWifiScanner* scanner = nullptr,
             IWifiCredentialsStore* store = nullptr);

  /**
   * Initializes WiFi setup and starts station or captive-portal mode.
   *
   * Call once from Arduino setup().
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Services the active portal or advances station reconnection.
   *
   * Call regularly from Arduino loop(); this method is non-blocking apart
   * from the platform web-server callbacks it dispatches.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Removes saved credentials so the portal is used on the next begin().
   *
   * @return Nothing.
   */
  void resetCredentials();

  // ========== Configuration ==========

  /**
   * Sets the maximum number of timed-out station attempts.
   *
   * @param maxTimeouts Failure limit; zero enables unlimited retries.
   * @return Nothing.
   */
  void setMaxTimeouts(unsigned long maxTimeouts);

  /**
   * Enables or disables restart after the timeout limit is reached.
   *
   * @param enable True to request a system restart at the limit.
   * @return Nothing.
   */
  void setRestartOnFailure(bool enable);

  // ========== Status ==========

  /**
   * Reports whether station mode is connected.
   *
   * @return True when the portal is inactive and station mode is connected.
   */
  bool isConnected() const;

  /**
   * Reports whether the captive portal is currently serving clients.
   *
   * @return True while portal mode is active.
   */
  bool isPortalActive() const;

  /**
   * Reports whether the device is operating as an access point.
   *
   * @return True while the captive portal access point is active.
   */
  bool isAccessPoint() const;

  /**
   * Returns the IP address for the currently active WiFi mode.
   *
   * @return Station IP when connected, otherwise the access-point IP.
   */
  IPAddress localIP() const;

  /**
   * Returns the received signal strength in station mode.
   *
   * @return RSSI in dBm, or zero while the portal is active.
   */
  int8_t rssi() const;

  /**
   * Writes a human-readable WiFi status line to the serial console.
   *
   * @return Nothing.
   */
  void reportStatus() const;

private:
  const char* _hostname;
  const char* _apPassword;
  IWifiStation& _wifiStation;
  IClock& _clock;
  ISystemControl& _system;
  IWifiScanner& _wifiScanner;
  IWifiCredentialsStore& _credentialsStore;
  WifiConnectionManager _connectionManager;
  WifiSetupController _setupController;
  WifiSetupPortalView _portalView;

  // ========== Runtime State ==========

  bool _portalActive = false;
  String _ssid;
  String _password;

  bool          _firstConnect       = true;

  // ========== Captive Portal ==========

  DNSServer   _dnsServer;
  WebServer   _server{80};

  // ========== Internal Helpers ==========

  void startStation();
  void startPortal();
  void handlePortal();
  void setupPortalRoutes();
};
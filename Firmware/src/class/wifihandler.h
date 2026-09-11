#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#include "IClock.h"
#include "ISystemControl.h"
#include "IWifiStation.h"
#include "WifiConnectionManager.h"

class WiFiHandler {
public:
  /**
   * @param hostname   Used as hostname in STA mode and as the AP SSID for the captive portal.
   * @param apPassword Optional password for the captive-portal AP (nullptr or "" = open network).
   * @param wifi Optional station driver; the ESP32 driver is used when omitted.
   * @param clock Optional monotonic clock; the ESP32 clock is used when omitted.
   * @param system Optional system control; the ESP32 control is used when omitted.
   */
  WiFiHandler(const char* hostname = "MQTTController-Setup",
              const char* apPassword = nullptr,
              IWifiStation* wifi = nullptr,
              IClock* clock = nullptr,
              ISystemControl* system = nullptr);

  // Call once in setup()
  void begin();

  // Call regularly in loop()
  void update();

  // Wipe saved credentials (forces portal on next boot)
  void resetCredentials();

  // Configuration methods
  void setMaxTimeouts(unsigned long maxTimeouts);
  void setRestartOnFailure(bool enable);

  // Status helpers
  bool isConnected() const;
  bool isPortalActive() const;
  bool isAccessPoint() const;
  IPAddress localIP() const;
  int8_t rssi() const;
  void reportStatus() const;

private:
  const char* _hostname;
  const char* _apPassword;
  IWifiStation& _wifiStation;
  IClock& _clock;
  ISystemControl& _system;
  WifiConnectionManager _connectionManager;

  // Runtime state
  bool _portalActive   = false;
  bool _hasCredentials = false;
  String _ssid;
  String _password;

  bool          _firstConnect       = true;

  // Captive portal objects
  DNSServer   _dnsServer;
  WebServer   _server{80};
  Preferences _prefs;

  // Internal helpers
  bool loadCredentials();
  void saveCredentials(const String& ssid, const String& pass);
  void startStation();
  void startPortal();
  void handlePortal();
  void setupPortalRoutes();
};
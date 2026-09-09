#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

class WiFiHandler {
public:
  /**
   * @param hostname   Used as hostname in STA mode and as the AP SSID for the captive portal.
   * @param apPassword Optional password for the captive-portal AP (nullptr or "" = open network).
   */
  WiFiHandler(const char* hostname = "MQTTController-Setup",
              const char* apPassword = nullptr);

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

  // Runtime state
  bool _portalActive   = false;
  bool _hasCredentials = false;
  String _ssid;
  String _password;

  // Station reconnect settings / state
  unsigned long _connectingMillis   = 0;
  unsigned long _connectionTimeout  = 30000;   // 30s per attempt
  unsigned long _timeouts           = 0;
  unsigned long _maxTimeouts        = 100;     // configurable
  bool          _restartOnFailure   = false;    // configurable
  bool          _firstConnect       = true;
  bool          _wasConnected       = false;

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
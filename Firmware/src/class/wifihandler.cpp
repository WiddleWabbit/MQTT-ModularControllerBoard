#include "wifihandler.h"

#include "Esp32Clock.h"
#include "Esp32SystemControl.h"
#include "Esp32WifiStation.h"

namespace {
Esp32WifiStation defaultWifiStation;
Esp32Clock defaultClock;
Esp32SystemControl defaultSystemControl;
}

// Simple clean captive-portal page
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Setup</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 400px; margin: 2rem auto; padding: 0 1rem; }
    h1 { font-size: 1.4rem; }
    input { width: 100%; padding: 0.6rem; margin: 0.4rem 0 1rem; box-sizing: border-box; }
    button { width: 100%; padding: 0.8rem; background: #007aff; color: white; border: none; border-radius: 6px; font-size: 1rem; }
  </style>
</head>
<body>
  <h1>WiFi Configuration</h1>
  <form action="/save" method="POST">
    <label>Network name (SSID)</label>
    <input type="text" name="ssid" required>
    <label>Password</label>
    <input type="password" name="pass">
    <button type="submit">Save & Connect</button>
  </form>
</body>
</html>
)rawliteral";

WiFiHandler::WiFiHandler(const char* hostname, const char* apPassword,
                         IWifiStation* wifi, IClock* clock,
                         ISystemControl* system)
  : _hostname(hostname),
    _apPassword(apPassword),
    _wifiStation(wifi == nullptr ? defaultWifiStation : *wifi),
    _clock(clock == nullptr ? defaultClock : *clock),
    _system(system == nullptr ? defaultSystemControl : *system),
    _connectionManager(_wifiStation, _clock, _system) {}

void WiFiHandler::begin() {
  _prefs.begin("wifi", false);

  if (loadCredentials()) {
    Serial.println("Found saved credentials – trying Station mode");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(_hostname);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    startStation();
  } else {
    Serial.println("No saved credentials – starting captive portal");
    startPortal();
  }
}

bool WiFiHandler::loadCredentials() {
  _ssid     = _prefs.getString("ssid", "");
  _password = _prefs.getString("pass", "");
  _hasCredentials = (_ssid.length() > 0);
  return _hasCredentials;
}

void WiFiHandler::saveCredentials(const String& ssid, const String& pass) {
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _ssid = ssid;
  _password = pass;
  _hasCredentials = true;
}

void WiFiHandler::resetCredentials() {
  _prefs.clear();
  _ssid = "";
  _password = "";
  _hasCredentials = false;
  Serial.println("Credentials cleared");
}

void WiFiHandler::startStation() {
  Serial.printf("Connecting to \"%s\"...\n", _ssid.c_str());
  _connectionManager.start(_ssid.c_str(), _password.c_str());
  _firstConnect = true;
  _portalActive = false;
}

void WiFiHandler::startPortal() {
  _portalActive = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_hostname, (_apPassword && _apPassword[0]) ? _apPassword : nullptr);

  IPAddress apIP = WiFi.softAPIP();
  Serial.println("Captive portal started");
  Serial.printf("  SSID    : %s\n", _hostname);
  Serial.printf("  Password: %s\n", (_apPassword && _apPassword[0]) ? _apPassword : "(open)");
  Serial.print  ("  IP      : ");
  Serial.println(apIP);

  // Redirect every DNS request to ourselves → triggers captive portal detection
  _dnsServer.start(53, "*", apIP);

  setupPortalRoutes();
  _server.begin();
}

void WiFiHandler::setupPortalRoutes() {
  _server.on("/", HTTP_GET, [this]() {
    _server.send_P(200, "text/html", PORTAL_HTML);
  });

  _server.on("/save", HTTP_POST, [this]() {
    if (!_server.hasArg("ssid") || _server.arg("ssid").isEmpty()) {
      _server.send(400, "text/plain", "SSID is required");
      return;
    }

    String newSsid = _server.arg("ssid");
    String newPass = _server.hasArg("pass") ? _server.arg("pass") : "";

    saveCredentials(newSsid, newPass);

    _server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;margin-top:3rem'>"
      "<h2>Saved!</h2><p>Connecting to the network and rebooting...</p></body></html>");

    delay(1500);
    ESP.restart();
  });

  // Catch-all - always show the form (important for captive portal)
  _server.onNotFound([this]() {
    _server.send_P(200, "text/html", PORTAL_HTML);
  });
}

void WiFiHandler::handlePortal() {
  _dnsServer.processNextRequest();
  _server.handleClient();
}

void WiFiHandler::update() {
  if (_portalActive) {
    handlePortal();
    return;
  }

  // ----- Normal Station mode -----
  const bool wasConnected = _connectionManager.isConnected();
  _connectionManager.update();
  const bool connected = _connectionManager.isConnected();

  if (connected) {
    if (_firstConnect) {
      Serial.print("WiFi connected – IP: ");
      Serial.println(WiFi.localIP());
      _firstConnect = false;
    }
    return;
  }

  // Lost connection
  if (wasConnected) {
    Serial.println("Lost WiFi connection – reconnecting...");
  }
}

// ========== Configuration Methods ==========

void WiFiHandler::setMaxTimeouts(unsigned long maxTimeouts) {
  _connectionManager.setMaxTimeouts(maxTimeouts);
}

void WiFiHandler::setRestartOnFailure(bool enable) {
  _connectionManager.setRestartOnFailure(enable);
}

// ========== Helper Functions ==========

bool WiFiHandler::isConnected() const {
  return !_portalActive && _connectionManager.isConnected();
}

bool WiFiHandler::isPortalActive() const {
  return _portalActive;
}

bool WiFiHandler::isAccessPoint() const {
  return _portalActive;
}

IPAddress WiFiHandler::localIP() const {
  return _portalActive ? WiFi.softAPIP() : WiFi.localIP();
}

int8_t WiFiHandler::rssi() const {
  return _portalActive ? 0 : WiFi.RSSI();
}

void WiFiHandler::reportStatus() const {
  if (_portalActive) {
    Serial.printf("[Portal] Active – %d client(s)  IP: %s\n",
                  WiFi.softAPgetStationNum(),
                  WiFi.softAPIP().toString().c_str());
  } else if (isConnected()) {
    Serial.printf("[STA] %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[STA] Not connected");
  }
}
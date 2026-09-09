#include "wifihandler.h"

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

WiFiHandler::WiFiHandler(const char* hostname, const char* apPassword)
  : _hostname(hostname), _apPassword(apPassword) {}

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
  WiFi.disconnect(true);
  WiFi.begin(_ssid.c_str(), _password.c_str());
  _connectingMillis = millis();
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
  const bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    if (_firstConnect) {
      Serial.print("WiFi connected – IP: ");
      Serial.println(WiFi.localIP());
      _timeouts = 0;
      _firstConnect = false;
    }
    _wasConnected = true;
    return;
  }

  // Lost connection
  if (_wasConnected) {
    Serial.println("Lost WiFi connection – reconnecting...");
    _wasConnected = false;
    startStation();
    return;
  }

  // Connection attempt timed out
  if (millis() - _connectingMillis >= _connectionTimeout) {
    _timeouts++;

    if (_restartOnFailure && _timeouts >= _maxTimeouts) {
      Serial.println("Too many failures – restarting...");
      ESP.restart();
    }

    // If we are not restarting, just keep trying (reset the counter optionally)
    if (!_restartOnFailure && _timeouts >= _maxTimeouts) {
      _timeouts = 0;   // prevent the counter from growing forever
    }

    Serial.printf("Timeout (attempt %lu/%lu) – retrying...\n",
                  _timeouts, _maxTimeouts);
    startStation();
  }
}

// ---------- Configuration Methods ----------

void WiFiHandler::setMaxTimeouts(unsigned long maxTimeouts) {
  _maxTimeouts = maxTimeouts;
}

void WiFiHandler::setRestartOnFailure(bool enable) {
  _restartOnFailure = enable;
}

// ---------- Helper Functions ----------

bool WiFiHandler::isConnected() const {
  return !_portalActive && (WiFi.status() == WL_CONNECTED);
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
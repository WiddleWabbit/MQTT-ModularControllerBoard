#include "wifihandler.h"
#include "Esp32Clock.h"
#include "Esp32SystemControl.h"
#include "Esp32WifiStation.h"
#include "Esp32WifiCredentialsStore.h"
#include "Esp32WifiScanner.h"
#include "Esp32Serial.h"

namespace {
Esp32WifiStation defaultWifiStation;
Esp32Clock defaultClock;
Esp32SystemControl defaultSystemControl;
Esp32WifiScanner defaultWifiScanner;
Esp32WifiCredentialsStore defaultCredentialsStore;
Esp32Serial defaultSerial;
}

WiFiHandler::WiFiHandler(const char* hostname, const char* apPassword,
                         IWifiStation* wifi, IClock* clock,
                         ISystemControl* system, IWifiScanner* scanner,
                         IWifiCredentialsStore* store, ISerial* serial)
  : _hostname(hostname),
    _apPassword(apPassword),
    _wifiStation(wifi == nullptr ? defaultWifiStation : *wifi),
    _clock(clock == nullptr ? defaultClock : *clock),
    _system(system == nullptr ? defaultSystemControl : *system),
    _wifiScanner(scanner == nullptr ? defaultWifiScanner : *scanner),
    _credentialsStore(store == nullptr ? defaultCredentialsStore : *store),
    _serial(serial == nullptr ? defaultSerial : *serial),
    _connectionManager(_wifiStation, _clock, _system),
    _setupController(_wifiScanner, _credentialsStore) {}

void WiFiHandler::begin() {
  _setupController.begin();

  if (_setupController.state() == WifiSetupState::StationReady) {
    const WifiCredentials& credentials = _setupController.credentials();
    _ssid = credentials.ssid;
    _password = credentials.password;
    _serial.println("Found saved credentials – trying Station mode");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(_hostname);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    startStation();
  } else {
    _serial.println("No saved credentials – starting captive portal");
    startPortal();
  }
}

void WiFiHandler::resetCredentials() {
  _setupController.clearCredentials();
  _ssid = "";
  _password = "";
  _serial.println("Credentials cleared");
}

void WiFiHandler::startStation() {
  _serial.printf("Connecting to \"%s\"...\n", _ssid.c_str());
  _connectionManager.start(_ssid.c_str(), _password.c_str());
  _firstConnect = true;
  _portalActive = false;
}

void WiFiHandler::startPortal() {
  _portalActive = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_hostname, (_apPassword && _apPassword[0]) ? _apPassword : nullptr);

  IPAddress apIP = WiFi.softAPIP();
  _serial.println("Captive portal started");
  _serial.printf("  SSID    : %s\n", _hostname);
  _serial.printf("  Password: %s\n", (_apPassword && _apPassword[0]) ? _apPassword : "(open)");
  _serial.print("  IP      : ");
  _serial.println(apIP.toString().c_str());

  // Redirect every DNS request to ourselves → triggers captive portal detection
  _dnsServer.start(53, "*", apIP);

  setupPortalRoutes();
  _server.begin();
}

void WiFiHandler::setupPortalRoutes() {
  _server.on("/", HTTP_GET, [this]() {
    const std::string html = _portalView.render(_setupController);
    _server.send(200, "text/html", html.c_str());
  });

  _server.on("/save", HTTP_POST, [this]() {
    const String selectedSsid =
        _server.hasArg("ssid") ? _server.arg("ssid") : "";
    const String manualSsid =
        _server.hasArg("manualSsid") ? _server.arg("manualSsid") : "";
    const String password =
        _server.hasArg("pass") ? _server.arg("pass") : "";

    if (!_setupController.submitCredentials(selectedSsid.c_str(),
                                            manualSsid.c_str(),
                                            password.c_str())) {
      _server.send(400, "text/plain",
                   "A valid SSID and password are required");
      return;
    }

    _server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;margin-top:3rem'>"
      "<h2>Saved!</h2><p>Connecting to the network and rebooting...</p></body></html>");

    delay(1500);
    ESP.restart();
  });

  // Catch-all - always show the form (important for captive portal)
  _server.onNotFound([this]() {
    const std::string html = _portalView.render(_setupController);
    _server.send(200, "text/html", html.c_str());
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
      _serial.print("WiFi connected – IP: ");
      _serial.println(WiFi.localIP().toString().c_str());
      _firstConnect = false;
    }
    return;
  }

  // Lost connection
  if (wasConnected) {
    _serial.println("Lost WiFi connection – reconnecting...");
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
    _serial.printf("[Portal] Active – %d client(s)  IP: %s\n",
                   WiFi.softAPgetStationNum(),
                   WiFi.softAPIP().toString().c_str());
  } else if (isConnected()) {
    _serial.printf("[STA] %s  RSSI: %d dBm\n",
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    _serial.println("[STA] Not connected");
  }
}
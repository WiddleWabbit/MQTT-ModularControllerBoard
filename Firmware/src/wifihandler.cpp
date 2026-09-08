#include "wifihandler.h"
#include <WiFi.h>

// Wifi Related Variables
const char* ssid = "ThePromisedLAN";
const char* wifiPass = "";
const char* hostName = "wateringController";
unsigned long disconnectedMillis = 0;
unsigned long connectingMillis = 0;
unsigned long connectionTimeout = 30000;
unsigned long currentMillis = 0;
unsigned long timeouts = 0;
unsigned long maxTimeouts = 100;
bool firstConnect = true;
bool firstDisconnect = false;

// Function to initialise wifi
void initWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName);

  Serial.println("Connecting to Wifi...");

  WiFi.disconnect();
  WiFi.begin(ssid, wifiPass);
  // Start connection timer
  connectingMillis = millis();

}

// Function to the current status of the wifi
void reportWiFiStatus() {

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wifi strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println("dBm");
  } else {
    Serial.println("Wifi Not Connected..");
  }

}

void checkWiFi() {

  currentMillis = millis();

  // If connected, print IP and set LED
  if (WiFi.status() == WL_CONNECTED) {
    // The first time we reconnect/connect
    if (firstConnect == true) {
      Serial.print("Wifi connected with IP: ");
      Serial.println(WiFi.localIP());
      WiFi.setAutoReconnect(true);
      WiFi.persistent(true);
      timeouts = 0;
      firstConnect = false;
      firstDisconnect = true;
    }

    disconnectedMillis = currentMillis; // Reset disconnect timer

  // WiFi is disconnected
  } else if (WiFi.status() != WL_CONNECTED) {

    // First discovery of disconnection
    if (firstDisconnect == true) {
      Serial.println("Lost wifi connection, attempting reconnect...");
      firstDisconnect = false;
      firstConnect = true;
      initWiFi();

    // Detected disconnection before, check timeout
    } else if (currentMillis - connectingMillis >= connectionTimeout) {

      if (timeouts >= maxTimeouts) {
        Serial.print("Timed out more than maximum times, resetting...");
        ESP.restart();
      }

      Serial.println("Wifi connection failed, retrying...");
      WiFi.disconnect();
      initWiFi();
      timeouts++;

    }
  }
}
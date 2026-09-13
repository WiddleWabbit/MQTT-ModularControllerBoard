#include <Arduino.h> // Arduino Code Library
#include <WiFi.h> // Library for Controlling Wifi
#include <Wire.h> // Wire Library to Communicate with I2C Devices
#include "class\wifihandler.h" // Custom Handler for Wifi
#include "Esp32Clock.h"
#include "Esp32WifiStation.h"
#include "Esp32NtpClient.h"
#include "Esp32MqttClient.h"
#include "Esp32Serial.h"
#include "NtpHandler.h"
#include "MqttManager.h"

// ========== Pin Configuration ==========

// Specify pins to use for I2C.
const uint8_t SDA_PIN = 4;
const uint8_t SCL_PIN = 5;
// Specify pins for SPI.
const uint8_t MOSI_PIN = 11;
const uint8_t MISO_PIN = 12;
const uint8_t SCK_PIN = 13;
// Specify chip-select pins.
const uint8_t CS1_PIN = 6;
const uint8_t CS2_PIN = 7;
const uint8_t CS3_PIN = 15;
const uint8_t CS4_PIN = 16;
// Specify sense pins.
const uint8_t SNS1_PIN = 39;
const uint8_t SNS2_PIN = 41;
const uint8_t SNS3_PIN = 44;
const uint8_t SNS4_PIN = 2;
// Specify general-use pins.
const uint8_t MOD1_PIN = 40;
const uint8_t MOD2_PIN = 42;
const uint8_t MOD3_PIN = 43;
const uint8_t MOD4_PIN = 1;
// Specify the USB Vbus sense pin.
const uint8_t VBUS_SNS_PIN = 8;

// ========== Service Construction ==========

Esp32Serial serial;
WiFiHandler wifi("MQTTController-Setup", "mqttcs", nullptr, nullptr,
                 nullptr, nullptr, nullptr, &serial);
Esp32WifiStation ntpWifi;
Esp32Clock ntpClock;
Esp32NtpClient ntpClient;
NtpHandler ntpHandler(ntpWifi, ntpClock, ntpClient);
MqttManager* mqttManager = nullptr;

// ========== Application Configuration ==========

// Broker values should be loaded from deployment configuration in production.
const char* MQTT_HOST = "mqtt.local";
const unsigned int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "watering-controller";
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

// ========== PSRAM Buffering ==========

const unsigned long PSRAM_BUFFER_OBJECTS = 1440; // 1 Day at one a Minute
const unsigned long PSRAM_SEND_FREQUENCY = 100; // Send every 100ms
unsigned long psramlastSend = 0;

/**
 * Creates and wires the MQTT services used for the lifetime of the application.
 *
 * Static local objects keep the services alive without exposing their
 * construction details as global state.
 *
 * @return Nothing.
 */
void setupMqtt()
{
  static MqttConfig mqttConfig;
  mqttConfig.host = MQTT_HOST;
  mqttConfig.port = MQTT_PORT;
  mqttConfig.clientId = MQTT_CLIENT_ID;
  mqttConfig.reconnectIntervalMs = MQTT_RECONNECT_INTERVAL_MS;

  static WiFiClient mqttNetworkClient;
  static Esp32MqttClient mqttClient(mqttNetworkClient, MQTT_HOST, MQTT_PORT);
  static MqttManager manager(ntpWifi, ntpClock, mqttClient, mqttConfig);
  mqttManager = &manager;
}

/**
 * Initializes serial output, PSRAM, WiFi setup, and I2C.
 *
 * @return Nothing.
 */
void setup()
{

  // ========== Serial ==========

  serial.begin(115200); // Initialize serial communication
  delay(2000); // Add a small delay so that serial is full initialised for setup.

  serial.println();
  serial.println("Powered on, Initialising..");

  // Configure local-time conversion before asynchronous NTP synchronization.
  ntpHandler.setTimezone("AWST-8");

  // ========== PSRAM Initialization ==========
  if (psramInit()) { 
    serial.println("PSRAM initialized");
    serial.print("Memory available in PSRAM : ");
    serial.println(static_cast<unsigned long>(ESP.getFreePsram()));
  } else {
    serial.println("PSRAM not found or initialization failed");
    return;
  }
  
  // ========== WiFi Setup ==========
  wifi.begin();
  setupMqtt();
  
  // ========== I2C ==========

  serial.println("Beginning I2C Communication.");
  Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C Communication

}

/**
 * Services WiFi and NTP, then reports runtime memory and connection status.
 *
 * @return Nothing.
 */
void loop()
{

  wifi.update(); // Check the wifi connection status and reconnect if necessary
  ntpHandler.update(); // Synchronize network time without blocking
  if (mqttManager != nullptr) {
    mqttManager->update(); // Maintain MQTT and dispatch incoming messages
  }
  wifi.reportStatus(); // Print wifi information for debugging
  
  // Debug printing
  serial.print("Free Heap Memory: ");
  serial.println(static_cast<unsigned long>(ESP.getFreeHeap()));
  serial.print("Free PSRAM: ");
  serial.println(static_cast<unsigned long>(ESP.getFreePsram()));

  delay(1000); // Set a delay so we don't loop too quickly

}
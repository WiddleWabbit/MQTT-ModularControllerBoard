#include <Arduino.h> // Arduino Code Library
#include <WiFi.h> // Library for Controlling Wifi
#include <Wire.h> // Wire Library to Communicate with I2C Devices
#include "Esp32Clock.h"
#include "Esp32WifiCredentialsStore.h"
#include "Esp32SystemControl.h"
#include "Esp32WifiStation.h"
#include "Esp32WifiScanner.h"
#include "Esp32NtpClient.h"
#include "Esp32MqttClient.h"
#include "Esp32Serial.h"
#include "WiFiHandler.h"
#include "NtpHandler.h"
#include "MqttManager.h"
#include "RuntimeCoordinator.h"

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
Esp32WifiStation wifiStation;
Esp32Clock systemClock;
Esp32SystemControl systemControl;
Esp32WifiScanner wifiScanner;
Esp32WifiCredentialsStore credentialsStore;
WiFiHandler wifi("MQTTController-Setup", "mqttcs", wifiStation, systemClock,
                 systemControl, wifiScanner, credentialsStore, serial);
Esp32NtpClient ntpClient;
NtpHandler ntpHandler(wifiStation, systemClock, ntpClient);

// ========== Application Configuration ==========

// Broker values should be loaded from deployment configuration in production.
constexpr char mqttHost[] = "mqtt.local";
constexpr unsigned int mqttPort = 1883;
constexpr char mqttClientId[] = "watering-controller";
constexpr unsigned long mqttReconnectIntervalMs = 5000;

/**
 * Builds the MQTT configuration used for the lifetime of the application.
 *
 * @return Complete broker and client configuration.
 */
MqttConfig createMqttConfig()
{
  MqttConfig config;
  config.host = mqttHost;
  config.port = mqttPort;
  config.clientId = mqttClientId;
  config.reconnectIntervalMs = mqttReconnectIntervalMs;
  return config;
}

MqttConfig mqttConfig = createMqttConfig();
WiFiClient mqttTransportClient;
Esp32MqttClient mqttClient(mqttTransportClient, mqttConfig.host.c_str(),
                           mqttConfig.port);
MqttManager mqttManager(wifiStation, systemClock, mqttClient, mqttConfig);
RuntimeCoordinator runtimeCoordinator(ntpHandler, mqttManager);

// ========== PSRAM Buffering ==========

const unsigned long PSRAM_BUFFER_OBJECTS = 1440; // 1 Day at one a Minute
const unsigned long PSRAM_SEND_FREQUENCY = 100; // Send every 100ms
unsigned long psramlastSend = 0;

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
  runtimeCoordinator.update(); // Synchronize NTP and maintain MQTT
  wifi.reportStatus(); // Print wifi information for debugging
  
  // Debug printing
  serial.print("Free Heap Memory: ");
  serial.println(static_cast<unsigned long>(ESP.getFreeHeap()));
  serial.print("Free PSRAM: ");
  serial.println(static_cast<unsigned long>(ESP.getFreePsram()));

  delay(1000); // Set a delay so we don't loop too quickly

}
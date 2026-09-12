#include <Arduino.h> // Arduino Code Library
#include <WiFi.h> // Library for Controlling Wifi
#include <Wire.h> // Wire Library to Communicate with I2C Devices
#include <PubSubClient.h> // Library to handle MQTT
#include "class\wifihandler.h" // Custom Handler for Wifi
#include "Esp32Clock.h"
#include "Esp32WifiStation.h"
#include "Esp32NtpClient.h"
#include "NtpHandler.h"

// Specify Pins to use for I2C
const uint8_t SDA_PIN = 4;
const uint8_t SCL_PIN = 5;
// Specify Pins for SPI
const uint8_t MOSI_PIN = 11;
const uint8_t MISO_PIN = 12;
const uint8_t SCK_PIN = 13;
// Specify Pins for CS Pins
const uint8_t CS1_PIN = 6;
const uint8_t CS2_PIN = 7;
const uint8_t CS3_PIN = 15;
const uint8_t CS4_PIN = 16;
// Specify Sense Pins
const uint8_t SNS1_PIN = 39;
const uint8_t SNS2_PIN = 41;
const uint8_t SNS3_PIN = 44;
const uint8_t SNS4_PIN = 2;
// Specify General Use Pins
const uint8_t MOD1_PIN = 40;
const uint8_t MOD2_PIN = 42;
const uint8_t MOD3_PIN = 43;
const uint8_t MOD4_PIN = 1;
// Specify USB Vbus Sense Pin
const uint8_t VBUS_SNS_PIN = 8;

// WiFi Initial Setup
WiFiHandler wifi("MQTTController-Setup", "mqttcs");
Esp32WifiStation ntpWifi;
Esp32Clock ntpClock;
Esp32NtpClient ntpClient;
NtpHandler ntpHandler(ntpWifi, ntpClock, ntpClient);

// PSRAM Buffering
const unsigned long PSRAM_BUFFER_OBJECTS = 1440; // 1 Day at one a Minute
const unsigned long PSRAM_SEND_FREQUENCY = 100; // Send every 100ms
unsigned long psramlastSend = 0;

void setup()
{

  // ---------- Begin Serial ----------

  Serial.begin(115200); // Initialize serial communication
  delay(2000); // Add a small delay so that serial is full initialised for setup.

  Serial.println();
  Serial.println("Powered on, Initialising..");

  // ---------- PSRAM Initialisation ----------
  if (psramInit()) { 
    Serial.println("PSRAM initialized");
    Serial.println((String)"Memory available in PSRAM : " +ESP.getFreePsram());
  } else {
    Serial.println("PSRAM not found or initialization failed");
    return;
  }
  
  // ---------- Begin WiFi Setup ----------
  wifi.begin();
  
  // ---------- Begin I2C ----------

  Serial.println("Beginning I2C Communication.");
  Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C Communication

}

void loop()
{

  wifi.update(); // Check the wifi connection status and reconnect if necessary
  ntpHandler.update(); // Synchronize network time without blocking
  wifi.reportStatus(); // Print wifi information for debugging
  
  // Debug printing
  Serial.print("Free Heap Memory: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM: ");
  Serial.println(ESP.getFreePsram());

  delay(1000); // Set a delay so we don't loop too quickly

}
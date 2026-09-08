#include <Arduino.h> // Arduino Code Library
#include <WiFi.h> // Library for Controlling Wifi
#include <Wire.h> // Wire Library to Communicate with I2C Devices
#include <PubSubClient.h> // Library to handle MQTT
#include "wifihandler.h" // Custom Handler for Wifi
#include "mqtthandler.h" // Custom handling and functions for MQTT
#include "jsonhandler.h" // Custom handling and functions for JSON
#include "timehandler.h" // Custom handling and functions for time & ntp sync
#include "class\PSRAM_Buffer.h" // Custom Class to create and maintain a PSRAM Buffer

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

// PSRAM Buffering
const unsigned long PSRAM_BUFFER_OBJECTS = 1440; // 1 Day at one a Minute
const unsigned long PSRAM_SEND_FREQUENCY = 100; // Send every 100ms
unsigned long psramlastSend = 0;

// Construct global instance of psram buffer
PSRAM_BUFFER* psram_buffer = nullptr;

void setup()
{

  Serial.begin(115200); // Initialize serial communication
  delay(2000); // Add a small delay so that serial is full initialised for setup.

  Serial.println();
  Serial.println("Powered on, Initialising..");

  if (psramInit()) { // Initialise PSRAM
    Serial.println("PSRAM initialized");
    Serial.println((String)"Memory available in PSRAM : " +ESP.getFreePsram());

    // Construct PSRAM Buffer to save readings to
    Serial.println("Creating PSRAM Buffer");
    Serial.print("Buffer Size of: ");
    Serial.println(PSRAM_BUFFER_OBJECTS);
    psram_buffer = new PSRAM_BUFFER(PSRAM_BUFFER_OBJECTS);
  } else {
    Serial.println("PSRAM not found or initialization failed");
    return;
  }
  
  initWiFi(); // Connect to the wifi network

  initNTP(); // Setup NTP Sync

  Serial.println("Beginning I2C Communication.");
  Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C Communication

}

void loop()
{

  checkWiFi(); // Check the wifi connection status and reconnect if necessary
  reportWiFiStatus(); // Print wifi information for debugging

  checkNTP(); // Check NTP Status, print time to serial every so often

  if (millis() - psramlastSend >= PSRAM_SEND_FREQUENCY) { // Send readings per interval set

    psramlastSend = millis();
    psram_buffer->sendSingleAndClear();

  }
  
  checkMQTT(); // Check the MQTT Status / Reconnect if needed
  if (mqttClient.connected()) {
    mqttClient.loop(); // Process the MQTT Client Tasks
  }

  // Debug printing
  Serial.print("Free Heap Memory: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM: ");
  Serial.println(ESP.getFreePsram());

  delay(1000); // Set a delay so we don't loop too quickly

}
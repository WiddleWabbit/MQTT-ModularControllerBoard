#include <Arduino.h> // Arduino Code Library
#include <WiFi.h> // Library for Controlling Wifi
#include <Wire.h> // Wire Library to Communicate with I2C Devices

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

// ========== PSRAM Buffering ==========

const unsigned long PSRAM_BUFFER_OBJECTS = 1440; // 1 Day at one a Minute
const unsigned long PSRAM_SEND_FREQUENCY = 100; // Send every 100ms
unsigned long psramlastSend = 0;

/**
 * Initializes the ESP32 and all functions.
 *
 * @return Nothing.
 */
void setup()
{

  // ========== Serial ==========

  Serial.begin(115200); // Initialize serial communication
  delay(2000); // Add a small delay so that serial is full initialised for setup.

  Serial.println();
  Serial.println("Powered on, Initialising..");

  // ========== PSRAM Initialization ==========
  if (psramInit()) { 
    Serial.println("PSRAM initialized");
    Serial.print("Memory available in PSRAM : ");
    Serial.println(static_cast<unsigned long>(ESP.getFreePsram()));
  } else {
    Serial.println("PSRAM not found or initialization failed");
    return;
  }
  
  // ========== I2C ==========

  Serial.println("Beginning I2C Communication.");
  Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C Communication

}

/**
 * Services WiFi and NTP, then reports runtime memory and connection status.
 *
 * @return Nothing.
 */
void loop()
{
  
  // Debug printing
  Serial.print("Free Heap Memory: ");
  Serial.println(static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.print("Free PSRAM: ");
  Serial.println(static_cast<unsigned long>(ESP.getFreePsram()));

  delay(1000); // Set a delay so we don't loop too quickly

}
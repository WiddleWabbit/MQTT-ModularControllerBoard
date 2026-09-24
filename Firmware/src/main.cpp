#include <Arduino.h> // Arduino Code Library
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h> // Wire Library to Communicate with I2C Devices

#include "Esp32Clock.h"
#include "Esp32DigitalPin.h"
#include "Esp32I2cMaster.h"
#include "Esp32PreferenceStore.h"
#include "PreferenceNetworkConfigStore.h"
#include "Esp32NtpAdapter.h"
#include "Esp32SerialPort.h"
#include "Esp32Wifi.h"
#include "ModuleHost.h"
#include "ModuleSlotPublisher.h"
#include "MqttService.h"
#include "SensorMqttBridge.h"
#include "SensorPoller.h"
#include "SolenoidMqttBridge.h"
#include "SolenoidPoller.h"
#include "NtpService.h"
#include "PubSubClientAdapter.h"
#include "NetworkRuntime.h"
#include "SerialConfigController.h"
#include "SerialStatusReporter.h"
#include "WifiManager.h"

// ========== Network Configuration ==========

const MqttSubscription mqttSubscriptions[] = {
  {kSolenoidCommandTopic, 1},
  {"watering/pump", 1},
  {kSensorReadTopic, 1}
};

// State of each solenoid output is read and published on this period.
const uint32_t kSolenoidPollIntervalMs = 60UL * 1000UL;

// Accepted watering/solenoids commands must arrive within this window.
// After 15 minutes with none, every solenoid output is turned off.
const uint32_t kSolenoidCommandTimeoutMs = 15UL * 60UL * 1000UL;

const char kSlotStatusTopicPrefix[] = "watering/slot";

Esp32Clock systemClock;
Esp32Wifi wifiDriver;
Esp32NtpAdapter ntpDriver;
WiFiClient mqttTransport;
PubSubClient pubSubClient(mqttTransport);
PubSubClientAdapter mqttDriver(pubSubClient);

WifiManager wifiManager(
  wifiDriver, systemClock,
  {"", "", 15000, 1000, 30000});
NtpService ntpService(
  ntpDriver, systemClock,
  {"pool.ntp.org", "time.nist.gov", nullptr, 28800, 0, 60000});
MqttService mqttService(
  mqttDriver, systemClock,
  {"watering-controller", nullptr, nullptr, mqttSubscriptions,
   sizeof(mqttSubscriptions) / sizeof(mqttSubscriptions[0]), 1000, 30000});
Esp32PreferenceStore networkPreferences("network");
PreferenceNetworkConfigStore networkConfigStore(networkPreferences);
Esp32SerialPort serialPort(Serial);
NetworkRuntime networkRuntime(
  networkConfigStore, wifiManager, mqttService);

// ========== Pin Configuration ==========

const uint8_t SDA_PIN = 4;
const uint8_t SCL_PIN = 5;
const uint8_t MOSI_PIN = 11;
// PCB/README/FSPI: MOSI=11, SCK=12, MISO=13. These two are swapped vs the PCB;
// do not use until PR 6.
const uint8_t MISO_PIN = 12;
const uint8_t SCK_PIN = 13;
const uint8_t CS1_PIN = 6;
const uint8_t CS2_PIN = 7;
const uint8_t CS3_PIN = 15;
const uint8_t CS4_PIN = 16;
const uint8_t SNS1_PIN = 39;
const uint8_t SNS2_PIN = 41;
const uint8_t SNS3_PIN = 44;
const uint8_t SNS4_PIN = 2;
const uint8_t MOD1_PIN = 40;
const uint8_t MOD2_PIN = 42;
const uint8_t MOD3_PIN = 43;
const uint8_t MOD4_PIN = 1;

Esp32DigitalPin sns1(SNS1_PIN);
Esp32DigitalPin sns2(SNS2_PIN);
Esp32DigitalPin sns3(SNS3_PIN);
Esp32DigitalPin sns4(SNS4_PIN);
Esp32DigitalPin mod1(MOD1_PIN);
Esp32DigitalPin mod2(MOD2_PIN);
Esp32DigitalPin mod3(MOD3_PIN);
Esp32DigitalPin mod4(MOD4_PIN);
Esp32DigitalPin cs1(CS1_PIN);
Esp32DigitalPin cs2(CS2_PIN);
Esp32DigitalPin cs3(CS3_PIN);
Esp32DigitalPin cs4(CS4_PIN);
Esp32I2cMaster i2cMaster(Wire, SDA_PIN, SCL_PIN);
SlotPins slotPins[4] = {
  {sns1, mod1, cs1},
  {sns2, mod2, cs2},
  {sns3, mod3, cs3},
  {sns4, mod4, cs4},
};
ModuleHost moduleHost(i2cMaster, systemClock, slotPins, ModuleHostConfig{});
SensorPoller sensorPoller(moduleHost, systemClock);
SensorMqttBridge sensorMqttBridge(
  sensorPoller, mqttService, kSlotStatusTopicPrefix);
SolenoidPoller solenoidPoller(
  moduleHost, systemClock, kSolenoidPollIntervalMs, kSolenoidCommandTimeoutMs);
SolenoidMqttBridge solenoidMqttBridge(
  solenoidPoller, mqttService, kSlotStatusTopicPrefix);
ModuleSlotPublisher moduleSlotPublisher(
  moduleHost, mqttService, kSlotStatusTopicPrefix);
SerialStatusReporter serialStatusReporter(
  serialPort, systemClock, wifiManager, ntpService, mqttService, moduleHost);
SerialConfigController serialConfigController(
  serialPort, networkRuntime, serialStatusReporter);


/**
 * Forwards one inbound MQTT payload to the sensor and solenoid bridges.
 * Each bridge ignores topics it does not own. Does not touch I2C.
 *
 * @param topic Received topic.
 * @param payload Payload bytes.
 * @param length Payload length.
 * @return Nothing.
 */
void dispatchMqttMessage(const char* topic, const uint8_t* payload,
                         size_t length, void*)
{
  SensorMqttBridge::onMqttMessage(topic, payload, length, &sensorMqttBridge);
  SolenoidMqttBridge::onMqttMessage(topic, payload, length, &solenoidMqttBridge);
}

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

  if (serialPort.isPlugged())
  {
    Serial.println();
    Serial.println("Powered on, Initialising..");
  }

  // ========== PSRAM Initialization ==========
  if (psramInit()) { 
    if (serialPort.isPlugged())
    {
      Serial.println("PSRAM initialized");
      Serial.print("Memory available in PSRAM : ");
      Serial.println(static_cast<unsigned long>(ESP.getFreePsram()));
    }
  } else {
    if (serialPort.isPlugged())
    {
      Serial.println("PSRAM not found or initialization failed");
    }
    return;
  }
  
  // ========== Modules ==========

  if (serialPort.isPlugged())
  {
    Serial.println("Beginning I2C Communication.");
  }
  moduleHost.begin();

  // ========== Networking ==========

  networkRuntime.begin(
    {"", "", "", 1883, "watering-controller", nullptr, nullptr,
     "watering-controller", true});
  if (serialPort.isPlugged())
  {
    const char* warning = networkConfigStore.loadWarning();
    if (warning != nullptr && warning[0] != '\0')
    {
      Serial.println(warning);
    }
  }
  ntpService.begin();
  serialStatusReporter.begin({10000});
  serialStatusReporter.setReportingEnabled(
    networkRuntime.config().statusReporting);
  mqttService.setMessageHandler(dispatchMqttMessage, nullptr);
}

/**
 * Services WiFi, NTP, MQTT, modules, and serial, polls sensor and
 * solenoid modules, publishes slot status and module readings, then
 * reports runtime memory.
 *
 * @return Nothing.
 */
void loop()
{
  wifiManager.update();
  ntpService.update();
  mqttService.update(wifiManager.isConnected());
  serialConfigController.update();
  moduleHost.update();
  sensorPoller.update();
  solenoidPoller.update();
  sensorMqttBridge.update();
  solenoidMqttBridge.update();
  moduleSlotPublisher.update();
  serialStatusReporter.update();

  static uint32_t lastReportAt = 0;
  const uint32_t now = systemClock.millis();
  if (static_cast<uint32_t>(now - lastReportAt) >= 30000)
  {
    lastReportAt = now;
    if (serialPort.isPlugged())
    {
      Serial.print("Free Heap Memory: ");
      Serial.println(static_cast<unsigned long>(ESP.getFreeHeap()));
      Serial.print("Free PSRAM: ");
      Serial.println(static_cast<unsigned long>(ESP.getFreePsram()));
    }
  }

  delay(10);

}

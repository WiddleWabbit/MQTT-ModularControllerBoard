#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "Esp32Clock.h"
#include "Esp32DigitalPin.h"
#include "Esp32I2cMaster.h"
#include "Esp32NtpAdapter.h"
#include "Esp32PreferenceStore.h"
#include "Esp32SerialPort.h"
#include "Esp32Wifi.h"
#include "ModuleHost.h"
#include "ModuleSlotPublisher.h"
#include "MqttService.h"
#include "NetworkRuntime.h"
#include "NtpService.h"
#include "PreferenceNetworkConfigStore.h"
#include "PubSubClientAdapter.h"
#include "SensorMqttBridge.h"
#include "SensorPoller.h"
#include "SerialConfigController.h"
#include "SerialStatusReporter.h"
#include "SolenoidMqttBridge.h"
#include "SolenoidPoller.h"
#include "WifiManager.h"

// TODO/NOTES
// ID for multiple board/s


// ========== Timing ==========

// Presence and a raw reading for each sensor input repeat on this period.
// Three failed count queries wait this long before the count is tried again.
const uint32_t kSensorPollIntervalMs = 60UL * 1000UL;

// State of each solenoid output is read and published on this period.
const uint32_t kSolenoidPollIntervalMs = 60UL * 1000UL;

// Accepted watering/solenoids commands must arrive within this window.
// After 15 minutes with none, every solenoid output is turned off.
const uint32_t kSolenoidCommandTimeoutMs = 15UL * 60UL * 1000UL;

// USB serial status snapshot. Reboot restores this; it is not stored.
const uint32_t kSerialStatusIntervalMs = 10UL * 1000UL;

// Heap and PSRAM lines on the USB serial port.
const uint32_t kMemoryReportIntervalMs = 30UL * 1000UL;


// ========== Network services ==========

const MqttSubscription mqttSubscriptions[] = {
  {kSolenoidCommandTopic, 1},
  {"watering/pump", 1},
  {kSensorReadTopic, 1}
};

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
NetworkRuntime networkRuntime(
  networkConfigStore, wifiManager, mqttService);


// ========== Board pins ==========

const uint8_t SDA_PIN = 4;
const uint8_t SCL_PIN = 5;
const uint8_t MOSI_PIN = 11;
const uint8_t MISO_PIN = 13;
const uint8_t SCK_PIN = 12;
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


// ========== Modules ==========

Esp32I2cMaster i2cMaster(Wire, SDA_PIN, SCL_PIN);
SlotPins slotPins[4] = {
  {sns1, mod1, cs1},
  {sns2, mod2, cs2},
  {sns3, mod3, cs3},
  {sns4, mod4, cs4},
};
ModuleHost moduleHost(i2cMaster, systemClock, slotPins, ModuleHostConfig{});
SensorPoller sensorPoller(moduleHost, systemClock, kSensorPollIntervalMs);
SensorMqttBridge sensorMqttBridge(
  sensorPoller, mqttService, kSlotStatusTopicPrefix);
SolenoidPoller solenoidPoller(
  moduleHost, systemClock, kSolenoidPollIntervalMs, kSolenoidCommandTimeoutMs);
SolenoidMqttBridge solenoidMqttBridge(
  solenoidPoller, mqttService, kSlotStatusTopicPrefix);
ModuleSlotPublisher moduleSlotPublisher(
  moduleHost, mqttService, kSlotStatusTopicPrefix);


// ========== Serial console ==========

Esp32SerialPort serialPort(Serial);
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
  Serial.begin(115200);
  delay(2000);

  if (serialPort.isPlugged())
  {
    Serial.println();
    Serial.println("Powered on, Initialising..");
  }

  if (psramInit())
  {
    if (serialPort.isPlugged())
    {
      Serial.println("PSRAM initialized");
      Serial.print("Memory available in PSRAM : ");
      Serial.println(static_cast<unsigned long>(ESP.getFreePsram()));
    }
  }
  else
  {
    if (serialPort.isPlugged())
    {
      Serial.println("PSRAM not found or initialization failed");
    }
    return;
  }

  if (serialPort.isPlugged())
  {
    Serial.println("Beginning I2C Communication.");
  }
  moduleHost.begin();

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
  serialStatusReporter.begin({kSerialStatusIntervalMs});
  serialStatusReporter.setReportingEnabled(
    networkRuntime.config().statusReporting);
  mqttService.setMessageHandler(dispatchMqttMessage, nullptr);
}


/**
 * One pass of the controller. Network and serial commands run first.
 * The host then takes at most one I2C transaction, and each poller
 * at most one query. MQTT publishes follow from what those pollers
 * stored. USB status lines are last.
 *
 * @return Nothing.
 */
void loop()
{
  // Advance connect, connected, or backoff. Does not block.
  wifiManager.update();
  // Record NTP sync, or retry configuration when its interval elapses.
  ntpService.update();
  // Keep the broker session once Wi-Fi is up. Inbound commands are
  // queued here for the pollers below.
  mqttService.update(wifiManager.isConnected());
  // Read complete USB serial lines (set, apply, status).
  serialConfigController.update();

  // At most one I2C transaction: enumerate a slot or health-ping one.
  moduleHost.update();
  // At most one sensor query: immediate read, input count, or the
  // next presence or reading.
  sensorPoller.update();
  // At most one solenoid query: output count, a changed on/off, the
  // command-absence failsafe, or the next state read.
  solenoidPoller.update();

  // Publish sensor readings stored above, including an unchanged value.
  sensorMqttBridge.update();
  // Publish solenoid states stored above, including an unchanged value.
  solenoidMqttBridge.update();
  // Publish a slot status line only when its text changed.
  moduleSlotPublisher.update();

  // Print Wi-Fi, NTP, MQTT, and the four slot lines when the snapshot
  // interval has elapsed and USB is plugged in.
  serialStatusReporter.update();

  // Print free heap and PSRAM when USB is plugged in.
  static uint32_t lastReportAt = 0;
  const uint32_t now = systemClock.millis();
  if (static_cast<uint32_t>(now - lastReportAt) >= kMemoryReportIntervalMs)
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

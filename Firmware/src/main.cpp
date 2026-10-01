#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "Esp32Clock.h"
#include "Esp32DigitalPin.h"
#include "Esp32I2cMaster.h"
#include "Esp32NtpAdapter.h"
#include "Esp32PreferenceStore.h"
#include "Esp32ProgrammingLatch.h"
#include "Esp32SerialPort.h"
#include "Esp32SpiMaster.h"
#include "Esp32Wifi.h"
#include "IspProgrammer.h"
#include "ModuleHost.h"
#include "ModuleSlotPublisher.h"
#include "MqttService.h"
#include "MqttTopicLayout.h"
#include "NetworkRuntime.h"
#include "NtpService.h"
#include "PreferenceNetworkConfigStore.h"
#include "ProgrammingSession.h"
#include "PubSubClientAdapter.h"
#include "PumpMqttBridge.h"
#include "PumpPoller.h"
#include "SensorMqttBridge.h"
#include "SensorPoller.h"
#include "SerialConfigController.h"
#include "SerialStatusReporter.h"
#include "SolenoidMqttBridge.h"
#include "SolenoidPoller.h"
#include "WifiManager.h"

// ========== Timing ==========

// Presence and a raw reading for each sensor input repeat on this period.
// Three failed count queries wait this long before the count is tried again.
const uint32_t kSensorPollIntervalMs = 60UL * 1000UL;

// State of each solenoid output is read and published on this period.
const uint32_t kSolenoidPollIntervalMs = 60UL * 1000UL;

// Accepted watering/solenoids commands must arrive within this window.
// After 15 minutes with none, every solenoid output is turned off.
const uint32_t kSolenoidCommandTimeoutMs = 15UL * 60UL * 1000UL;

// State of the pump is read and published on this period.
const uint32_t kPumpPollIntervalMs = 60UL * 1000UL;

// Accepted watering/pump on/off commands must arrive within this window.
// After 3 minutes with none, a pump that is on is turned off.
// A reset command does not refresh this window.
const uint32_t kPumpCommandTimeoutMs = 3UL * 60UL * 1000UL;

// USB serial status snapshot. Reboot restores this; it is not stored.
const uint32_t kSerialStatusIntervalMs = 10UL * 1000UL;

// Heap and PSRAM lines on the USB serial port.
const uint32_t kMemoryReportIntervalMs = 30UL * 1000UL;

// No STK500 byte for this long ends the ISP session and resumes the
// module host. Solenoid and pump absence windows keep counting.
const uint32_t kProgrammingIdleTimeoutMs = 60UL * 1000UL;

// After the USB link has been seen, this long with no frames ends the
// ISP session. A monitor close or open can stop frames for less than this.
const uint32_t kProgrammingUnplugTimeoutMs = 1000UL;


// ========== Network services ==========

// Topic root used when mqtt_prefix is not stored. One path segment.
const char kMqttDeviceId[] = "watering";

Esp32Clock systemClock;
Esp32Wifi wifiDriver;
Esp32NtpAdapter ntpDriver;
WiFiClient mqttTransport;
PubSubClient pubSubClient(mqttTransport);
PubSubClientAdapter mqttDriver(pubSubClient);
MqttTopicLayout mqttTopics(kMqttDeviceId);

WifiManager wifiManager(
  wifiDriver, systemClock,
  {"", "", 15000, 1000, 30000});
NtpService ntpService(
  ntpDriver, systemClock,
  {"pool.ntp.org", "time.nist.gov", nullptr, 28800, 0, 60000});
MqttService mqttService(
  mqttDriver, systemClock,
  {"watering-controller", nullptr, nullptr, mqttTopics.subscriptions(),
   mqttTopics.subscriptionCount(), 1000, 30000});
Esp32PreferenceStore networkPreferences("network");
PreferenceNetworkConfigStore networkConfigStore(networkPreferences);
NetworkRuntime networkRuntime(
  networkConfigStore, wifiManager, mqttService, mqttTopics);


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
  sensorPoller, mqttService, mqttTopics);
SolenoidPoller solenoidPoller(
  moduleHost, systemClock, kSolenoidPollIntervalMs, kSolenoidCommandTimeoutMs);
SolenoidMqttBridge solenoidMqttBridge(
  solenoidPoller, mqttService, mqttTopics);
PumpPoller pumpPoller(
  moduleHost, systemClock, kPumpPollIntervalMs, kPumpCommandTimeoutMs);
PumpMqttBridge pumpMqttBridge(
  pumpPoller, mqttService, mqttTopics);
ModuleSlotPublisher moduleSlotPublisher(
  moduleHost, mqttService, mqttTopics);


// ========== Serial console ==========

Esp32SerialPort serialPort(Serial);
SerialStatusReporter serialStatusReporter(
  serialPort, systemClock, wifiManager, ntpService, mqttService, moduleHost);
SerialConfigController serialConfigController(
  serialPort, networkRuntime, serialStatusReporter);
Esp32SpiMaster ispSpi(SCK_PIN, MISO_PIN, MOSI_PIN);
IspProgrammer ispProgrammer(serialPort, ispSpi, cs1, systemClock);
Esp32ProgrammingLatch programmingLatch;
ProgrammingSession programmingSession(
  ispProgrammer, moduleHost, cs1, serialPort, systemClock,
  kProgrammingIdleTimeoutMs, kProgrammingUnplugTimeoutMs);
bool startupAttempted = false;


/**
 * Forwards one inbound MQTT payload to the sensor, solenoid, and pump
 * bridges. Each bridge ignores topics it does not own. Does not touch I2C.
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
  PumpMqttBridge::onMqttMessage(topic, payload, length, &pumpMqttBridge);
}


/**
 * Starts PSRAM, the module host, and network services once.
 * A programming session that began from the RTC latch calls this
 * after the session ends. A failed PSRAM init does not try again.
 *
 * @return Nothing.
 */
void startController()
{
  startupAttempted = true;

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
     "watering-controller", true, kMqttDeviceId});
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
 * Opens USB serial. A latched ISP session starts immediately so an
 * avrdude port-open restart reaches STK500 before the banner delay.
 *
 * @return Nothing.
 */
void setup()
{
  Serial.begin(115200);
  if (programmingLatch.isSet())
  {
    programmingSession.begin();
    return;
  }
  delay(2000);
  startController();
}


/**
 * One pass of the controller. Network services always run. While an
 * ISP session is active the USB byte stream belongs to STK500, and
 * the module host, pollers, and status lines wait. Otherwise serial
 * commands run, then the host takes at most one I2C transaction and
 * each poller at most one query. MQTT publishes follow from what
 * those pollers stored. USB status lines are last.
 *
 * @return Nothing.
 */
void loop()
{
  // Advance connect, connected, or backoff. Does not block.
  // Idle until startController(), including during a latched session.
  wifiManager.update();
  // Record NTP sync, or retry configuration when its interval elapses.
  ntpService.update();
  // Keep the broker session once Wi-Fi is up. Inbound commands are
  // queued here. Pollers below apply them once programming ends.
  mqttService.update(wifiManager.isConnected());

  if (programmingSession.active())
  {
    // STK500 only. No console lines, I2C, pollers, or status.
    programmingSession.update();
    if (programmingSession.active())
    {
      return;
    }
    programmingLatch.clear();
    if (!startupAttempted)
    {
      startController();
    }
  }

  // Read complete USB serial lines (set, apply, status, program).
  serialConfigController.update();
  if (serialConfigController.takeProgrammingRequest())
  {
    // Store the latch before the session so a USB restart re-enters.
    programmingLatch.set();
    programmingSession.begin();
    return;
  }

  // At most one I2C transaction: enumerate a slot or health-ping one.
  moduleHost.update();
  // At most one sensor query: immediate read, input count, or the
  // next presence or reading.
  sensorPoller.update();
  // At most one solenoid query: output count, a changed on/off, the
  // command-absence failsafe, or the next state read.
  solenoidPoller.update();
  // At most one pump query: a first state read, a reset, a changed
  // on/off, the command-absence failsafe, or the next state read.
  pumpPoller.update();

  // Publish sensor readings stored above, including an unchanged value.
  sensorMqttBridge.update();
  // Publish solenoid states stored above, including an unchanged value.
  solenoidMqttBridge.update();
  // Publish the pump state stored above, including an unchanged value.
  pumpMqttBridge.update();
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

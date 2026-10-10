#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "Esp32Clock.h"
#include "Esp32DigitalPin.h"
#include "Esp32HalfDuplexUart.h"
#include "Esp32I2cMaster.h"
#include "Esp32NtpAdapter.h"
#include "Esp32PreferenceStore.h"
#include "Esp32ProgrammingLatch.h"
#include "Esp32SerialPort.h"
#include "Esp32SpiMaster.h"
#include "Esp32Wifi.h"
#include "ModuleBus.h"
#include "Network.h"
#include "Programming.h"
#include "PubSubClientAdapter.h"
#include "PumpModule.h"
#include "SensorModule.h"
#include "SerialConsole.h"
#include "SolenoidModule.h"

// ========== Timing ==========

// Presence and a raw reading for each sensor input repeat on this period.
// Three failed count queries wait this long before the count is tried again.
const uint32_t kSensorPollIntervalMs = 60UL * 1000UL;

// State of each solenoid output is read and published on this period.
const uint32_t kSolenoidPollIntervalMs = 60UL * 1000UL;

// Accepted solenoid commands must arrive within this window.
// After 15 minutes with none, every solenoid output is turned off.
const uint32_t kSolenoidCommandTimeoutMs = 15UL * 60UL * 1000UL;

// State of the pump is read and published on this period.
const uint32_t kPumpPollIntervalMs = 60UL * 1000UL;

// Accepted pump on/off commands must arrive within this window.
// After 3 minutes with none, a pump that is on is turned off.
// A reset command does not refresh this window.
const uint32_t kPumpCommandTimeoutMs = 3UL * 60UL * 1000UL;

// USB serial status snapshot. Reboot restores this; it is not stored.
const uint32_t kSerialStatusIntervalMs = 10UL * 1000UL;

// Heap and PSRAM lines on the USB serial port.
const uint32_t kMemoryReportIntervalMs = 30UL * 1000UL;

// No protocol byte for this long ends the programming session and
// resumes the module host. Solenoid and pump absence windows keep counting.
const uint32_t kProgrammingIdleTimeoutMs = 60UL * 1000UL;

// After the USB link has been seen, this long with no frames ends the
// programming session. A monitor close or open can stop frames for less
// than this.
const uint32_t kProgrammingUnplugTimeoutMs = 1000UL;

const NetworkTiming kNetworkTiming = {15000, 1000, 30000, 1000, 30000};

const NtpConfig kNtpConfig = {
  "pool.ntp.org", "time.nist.gov", nullptr, 28800, 0, 60000};


// ========== Network ==========

// Topic root used when mqtt_prefix is not stored. One path segment.
const char kMqttDeviceId[] = "watering";

Esp32Clock systemClock;
Esp32Wifi wifiDriver;
Esp32NtpAdapter ntpDriver;
WiFiClient mqttTransport;
PubSubClient pubSubClient(mqttTransport);
PubSubClientAdapter mqttDriver(pubSubClient);
Esp32PreferenceStore networkPreferences("network");
Network network(wifiDriver, ntpDriver, mqttDriver, systemClock,
                networkPreferences, kMqttDeviceId, kNtpConfig, kNetworkTiming);


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
ModuleBus moduleBus(i2cMaster, systemClock, slotPins, ModuleHostConfig{},
                    network);
SensorModule sensorModule(moduleBus, network, systemClock,
                          kSensorPollIntervalMs);
SolenoidModule solenoidModule(moduleBus, network, systemClock,
                              kSolenoidPollIntervalMs,
                              kSolenoidCommandTimeoutMs);
PumpModule pumpModule(moduleBus, network, systemClock, kPumpPollIntervalMs,
                      kPumpCommandTimeoutMs);


// ========== Serial console and programming ==========

Esp32SerialPort serialPort(Serial);
const ProgrammingPins kProgrammingPins = {
  MOSI_PIN, MISO_PIN, SCK_PIN, {CS1_PIN, CS2_PIN, CS3_PIN, CS4_PIN}};
SerialConsole serialConsole(serialPort, systemClock, network, moduleBus,
                            kSerialStatusIntervalMs, kProgrammingPins);
Esp32SpiMaster ispSpi(SCK_PIN, MISO_PIN, MOSI_PIN);
Esp32HalfDuplexUart updiUart;
Esp32ProgrammingLatch programmingLatch;
Programming programming(serialPort, ispSpi, updiUart, cs1, cs2, cs3, cs4,
                        kProgrammingPins, moduleBus, systemClock,
                        kProgrammingIdleTimeoutMs,
                        kProgrammingUnplugTimeoutMs);
bool startupAttempted = false;


/**
 * Starts PSRAM, the module bus, and network services once.
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
  moduleBus.begin();

  network.begin(
    {"", "", "", 1883, "watering-controller", nullptr, nullptr,
     "watering-controller", true, kMqttDeviceId});
  if (serialPort.isPlugged())
  {
    const char* warning = network.loadWarning();
    if (warning != nullptr && warning[0] != '\0')
    {
      Serial.println(warning);
    }
  }
  serialConsole.begin();
}


/**
 * Opens USB serial. A latched programming session starts immediately
 * so an avrdude port-open restart reaches the programmer before the
 * banner delay.
 *
 * @return Nothing.
 */
void setup()
{
  Serial.begin(115200);
  if (programmingLatch.isSet())
  {
    const ProgrammingMethod method = programmingLatch.method() == 2
                                       ? ProgrammingMethod::Updi
                                       : ProgrammingMethod::Isp;
    programming.begin(programmingLatch.slot(), method);
    return;
  }
  delay(2000);
  startController();
}


/**
 * One pass of the controller. Network always runs. While a programming
 * session is active the USB byte stream belongs to ISP or UPDI, and the
 * module bus, daughter modules, and status lines wait. Otherwise the
 * console reads commands, the bus takes at most one I2C transaction,
 * and each daughter module takes at most one exchange and publishes
 * what it stored. The periodic USB snapshot is last.
 *
 * @return Nothing.
 */
void loop()
{
  // Advance Wi-Fi, NTP, and MQTT. Inbound commands are queued here.
  // Idle until startController(), including during a latched session.
  network.update();

  if (programming.active())
  {
    // Programming session only. No console lines, I2C, modules, or status.
    programming.update();
    if (programming.active())
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
  serialConsole.update();
  ProgrammingRequest request;
  if (serialConsole.takeProgrammingRequest(request))
  {
    // Store the latch before the session so a USB restart re-enters.
    programmingLatch.set(request.slot, static_cast<uint8_t>(request.method));
    programming.begin(request.slot, request.method);
    return;
  }

  // At most one I2C transaction, then slot status if its text changed.
  moduleBus.update();
  // At most one sensor exchange, then publish what it stored.
  sensorModule.update();
  // At most one solenoid exchange, then publish what it stored.
  solenoidModule.update();
  // At most one pump exchange, then publish what it stored.
  pumpModule.update();

  // Print Wi-Fi, NTP, MQTT, and the four slot lines when the snapshot
  // interval has elapsed and USB is plugged in.
  serialConsole.updateStatus();

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

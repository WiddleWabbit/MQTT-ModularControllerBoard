# Firmware architecture

The firmware follows a dependency-inverted `Logic -> Interfaces <- Drivers`
design. Business logic is written against small abstract interfaces and does
not include Arduino headers. Hardware adapters are isolated in
`lib/Drivers/`, while reusable stateful services live in `lib/Logic/`.

## Component boundaries

- `lib/Interfaces/` contains pure abstract hardware contracts only.
- `lib/Drivers/` contains ESP32/Arduino implementations of those contracts.
- `lib/Logic/` contains nonblocking WiFi, NTP, MQTT, and four-slot module
  host state machines, USB-gated serial status reporting, and retained MQTT
  publication of slot snapshots.
- `test/test_desktop/fakes/` contains controllable implementations for native
  tests.
- `test/test_desktop/` contains Unity unit and interaction tests.

Dependencies flow toward interfaces. For example, `MqttService` knows only
`IMqttClient` and `IClock`; it does not know PubSubClient, WiFiClient, or
ESP32 APIs. This keeps each module deep: callers invoke `begin`, `update`, or
`publish` while retry timing, callbacks, and subscriptions remain internal.

## Runtime composition

`src/main.cpp` constructs the ESP32 drivers, injects them into the services,
and calls each service from `loop()`. `Esp32PreferenceStore` hides NVS, and
`PreferenceNetworkConfigStore` loads and saves individual fields.
`NetworkRuntime` applies a field to WiFi or MQTT only after that field has been
persisted. `SerialConfigController` stages line-oriented commands and, on
`apply`, persists only the fields set since the previous successful apply.
`SerialStatusReporter` writes WiFi, NTP, MQTT, and slot snapshots on a session
interval started from `setup()` while USB serial is plugged in and periodic
reporting is enabled. The on/off flag and the DHCP hostname are staged with
the other `set` commands and stored on `apply`. `status` prints one snapshot
immediately. `Esp32SerialPort` uses the
ESP32 USB CDC plug state to gate serial input and responses.
`ModuleHost` owns the shared I2C bus. It configures per-slot sense (input
pull-up, LOW = present), MOD (open-drain enumeration select), and CS (idle
pull-up), then runs a nonblocking per-slot state machine from `loop()`.
Unconfigured modules share address `0x0A`; the host selects one slot at a
time with MOD, assigns `0x10 + slot`, and identifies the type. At most one
I2C protocol transaction runs per `ModuleHost::update()`.
`ModuleSlotPublisher` publishes a retained MQTT snapshot when a slot's public
status text changes. `loop()` calls it after `moduleHost.update()`.
`ModuleHost` does not depend on the MQTT client, and the publisher does not
call `ping()` or `echo()`.
A Sensor module (`0x0200`) is polled by `SensorPoller` after
`moduleHost.update()`. The poller asks for the input count when that module
is identified, then presence and a raw reading for each input once a minute.
`SensorMqttBridge` publishes every stored reading, including a repeated
value, and accepts `watering/sensor/read` as an immediate read. The MQTT
callback only enqueues the request. Behaviour, topics, and commands are in
[SENSORMODULE.md](SENSORMODULE.md).
`update()` methods never wait for a network operation. WiFi and MQTT retries
use wrap-safe elapsed-time checks and exponential backoff. I2C transactions
are bounded by a 50 ms driver timeout.

## Desktop testing

Tests run exclusively with PlatformIO's `native` environment:

```text
pio test -e native
```

The fakes simulate link state, RSSI, time, broker outcomes, subscriptions,
publications, inbound messages, persisted settings, USB presence, serial
bytes, GPIO levels, and I2C slaves. This covers state transitions, retry
backoff, failures, callbacks, configuration staging, apply failure,
WiFi-to-MQTT interaction, serial status snapshots, hot-plug enumeration,
module protocol frames, retained slot-status publication, and sensor
count/presence/reading polls including an immediate MQTT read, without hardware.
Production ESP32 builds use only `lib/Drivers/`; test code and fakes are not
included.

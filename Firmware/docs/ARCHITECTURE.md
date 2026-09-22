# Firmware architecture

The firmware follows a dependency-inverted `Logic -> Interfaces <- Drivers`
design. Business logic is written against small abstract interfaces and does
not include Arduino headers. Hardware adapters are isolated in
`lib/Drivers/`, while reusable stateful services live in `lib/Logic/`.

## Component boundaries

- `lib/Interfaces/` contains pure abstract hardware contracts only.
- `lib/Drivers/` contains ESP32/Arduino implementations of those contracts.
- `lib/Logic/` contains nonblocking WiFi, NTP, MQTT, and four-slot module
  host state machines plus USB-gated serial status reporting.
- `test/test_desktop/fakes/` contains controllable implementations for native
  tests.
- `test/test_desktop/` contains Unity unit and interaction tests.

Dependencies flow toward interfaces. For example, `MqttService` knows only
`IMqttClient` and `IClock`; it does not know PubSubClient, WiFiClient, or
ESP32 APIs. This keeps each module deep: callers invoke `begin`, `update`, or
`publish` while retry timing, callbacks, and subscriptions remain internal.

## Runtime composition

`src/main.cpp` constructs the ESP32 drivers, injects them into the services,
and calls each service from `loop()`. `Esp32NetworkConfigStore` hides NVS
storage, while `NetworkRuntime` applies a complete configuration to WiFi and
MQTT only after it has been persisted. `SerialConfigController` stages line-oriented commands and changes runtime
settings only for an explicit `apply` command. `SerialStatusReporter` writes
WiFi, NTP, and MQTT snapshots on a session interval started from `setup()`
while USB serial is plugged in. `Esp32SerialPort` uses the
ESP32 USB CDC plug state to gate serial input and responses.
`ModuleHost` owns the shared I2C bus. It configures per-slot sense (input
pull-up, LOW = present), MOD (open-drain enumeration select), and CS (idle
pull-up), then runs a nonblocking per-slot state machine from `loop()`.
Unconfigured modules share address `0x0A`; the host selects one slot at a
time with MOD, assigns `0x10 + slot`, and identifies the type. At most one
I2C protocol transaction runs per `ModuleHost::update()`.
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
and module protocol frames without hardware.
Production ESP32 builds use only `lib/Drivers/`; test code and fakes are not
included.

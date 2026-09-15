# Firmware architecture

The firmware follows a dependency-inverted `Logic -> Interfaces <- Drivers`
design. Business logic is written against small abstract interfaces and does
not include Arduino headers. Hardware adapters are isolated in
`lib/Drivers/`, while reusable stateful services live in `lib/Logic/`.

## Component boundaries

- `lib/Interfaces/` contains pure abstract hardware contracts only.
- `lib/Drivers/` contains ESP32/Arduino implementations of those contracts.
- `lib/Logic/` contains nonblocking WiFi, NTP, and MQTT state machines.
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
settings only for an explicit `apply` command. `Esp32SerialPort` uses the
ESP32 USB CDC plug state to gate serial input and responses.
`update()` methods never wait for a network operation. WiFi and MQTT retries
use wrap-safe elapsed-time checks and exponential backoff.

## Desktop testing

Tests run exclusively with PlatformIO's `native` environment:

```text
pio test -e native
```

The fakes simulate link state, time, broker outcomes, subscriptions,
publications, inbound messages, persisted settings, USB presence, and serial
bytes. This covers state transitions, retry backoff, failures, callbacks,
configuration staging, apply failure, and WiFi-to-MQTT interaction without
hardware.
Production ESP32 builds use only `lib/Drivers/`; test code and fakes are not
included.

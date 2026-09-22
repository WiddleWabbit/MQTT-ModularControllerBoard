# Networking services

## WiFi

`WifiManager` owns the states `Idle`, `Connecting`, `Connected`, and `Backoff`.
`begin()` starts one station attempt. `update()` observes the injected
`IWifi`, times out unsuccessful attempts, and retries with capped exponential
backoff. It never delays or waits for a connection.

`IWifi` exposes `begin`, `status`, `rssi`, and `disconnect`. The ESP32
implementation is `Esp32Wifi`; native tests use `FakeWifi`, which records
credentials and disconnects, allows each link state to be selected, and
exposes a settable RSSI. `WifiManager::rssi()` forwards the driver value.

## NTP

`NtpService` configures the injected `INtpAdapter` and reports `Idle`,
`WaitingForSync`, or `Synchronized`. It periodically reapplies configuration
while waiting, then exposes the synchronized epoch through `currentTime()`.
`config()` returns the active servers, UTC/DST offsets, and retry interval.
`Esp32NtpAdapter` calls Arduino `configTime` and considers an epoch after
November 2023 valid. `FakeNtpAdapter` records servers and offsets and can
toggle synchronization.

## MQTT

`MqttService` owns `Idle`, `WaitingForNetwork`, `Connecting`, `Connected`, and
`Backoff` states. `update(networkReady)` waits for WiFi, reconnects without
blocking, subscribes after a successful connection, services the MQTT loop,
and disconnects when WiFi is lost. The service only enters `Connected` after
every configured subscription succeeds; a subscription failure closes the
broker connection and uses the normal reconnect backoff. `publish` is rejected
while disconnected.
Inbound payloads are forwarded through `MqttMessageCallback`.

`IMqttClient` is the narrow client contract. `PubSubClientAdapter` wraps an
already-constructed PubSubClient and is configured with `setServer`.
`FakeMqttClient` provides deterministic connection failures, subscription and
publication results, loop counts, and inbound callback delivery.

## Serial status

`SerialStatusReporter` writes WiFi, NTP, MQTT, and four slot lines through
`ISerialPort` on a configured snapshot interval while USB serial is plugged in.
`begin(config)` starts reporting for the power-on session; `reconfigure(config)`
replaces the interval without stopping. The interval is not persisted to NVS.
`setup()` currently starts reporting at 1000 ms.

It uses `WifiManager` state names (`Idle`, `Connecting`, `Connected`,
`Backoff`) and appends RSSI only when connected. MQTT labels match
`MqttServiceState` (`Idle`, `WaitingForNetwork`, `Connecting`, `Connected`,
`Backoff`):

```text
WiFi Status: Connected (-62 dBm)
NTP Status: Synchronized (2026-09-21 16:04:00)
MQTT Status: Connected
Slot 1: Empty
Slot 2: Online IdentityEcho addr=0x11
Slot 3: Unsupported type=0x02AA addr=0x12
Slot 4: Fault Nack
```

Unsynchronized NTP omits the timestamp: `NTP Status: WaitingForSync`. Local
time is the UTC epoch plus the NTP UTC and daylight offsets, formatted as
`YYYY-MM-DD HH:MM:SS` without `localtime()`. Slot lines use public
`ModuleHost` snapshots only; the reporter never calls `ping()` or `echo()`.
Firmware Slot 1 is index 0 / address `0x10`. Native tests pin the epoch and
offsets, cover plug gating, interval spacing, `begin`/`reconfigure`, MQTT
states, slot occupancy, and advancing time after sync.

## Configuration and testing

Network credentials and broker settings are composed in `src/main.cpp`.
Credentials should be supplied in the deployment configuration rather than
committed to source control. Native tests use injected `FakeClock` instances
to advance retry windows deterministically and verify all public service
operations and cross-service interactions.

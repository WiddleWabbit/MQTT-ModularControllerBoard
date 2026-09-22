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

`MqttService` owns `Idle`, `Unconfigured`, `WaitingForNetwork`, `Connecting`,
`Connected`, and `Backoff` states. An empty broker host stays `Unconfigured`
and does not call `connect`, so the ESP32 DNS resolver is not asked to look up
a blank name. `update(networkReady)` otherwise waits for WiFi, reconnects
without blocking, subscribes after a successful connection, services the MQTT
loop, and disconnects when WiFi is lost. The service only enters `Connected`
after every configured subscription succeeds; a subscription failure closes the
broker connection and uses the normal reconnect backoff. `publish` is rejected
while disconnected.
Inbound payloads are forwarded through `MqttMessageCallback`.

`IMqttClient` is the narrow client contract. `PubSubClientAdapter` wraps an
already-constructed PubSubClient and is configured with `setServer`.
`FakeMqttClient` provides deterministic connection failures, subscription and
publication results, loop counts, and inbound callback delivery.

## Serial status

`SerialStatusReporter` writes WiFi, NTP, and MQTT lines through `ISerialPort`
on a configured snapshot interval while USB serial is plugged in.
`begin(config)` starts reporting for the power-on session; `reconfigure(config)`
replaces the interval without stopping. The interval is not persisted to NVS.
`setup()` currently starts reporting at 1000 ms.

It uses `WifiManager` state names (`Idle`, `Connecting`, `Connected`,
`Backoff`) and appends RSSI only when connected. MQTT labels match
`MqttServiceState` (`Idle`, `Unconfigured`, `WaitingForNetwork`, `Connecting`,
`Connected`, `Backoff`):

```text
WiFi Status: Connected (-62 dBm)
NTP Status: Synchronized (2026-09-21 16:04:00)
MQTT Status: Connected
```

Unsynchronized NTP omits the timestamp: `NTP Status: WaitingForSync`. Local
time is the UTC epoch plus the NTP UTC and daylight offsets, formatted as
`YYYY-MM-DD HH:MM:SS` without `localtime()`. Native tests pin the epoch and
offsets, cover plug gating, interval spacing, `begin`/`reconfigure`, MQTT
states, and advancing time after sync.

## Configuration and testing

Network credentials and broker settings are composed in `src/main.cpp`.
Credentials should be supplied in the deployment configuration rather than
committed to source control. Native tests use injected `FakeClock` instances
to advance retry windows deterministically and verify all public service
operations and cross-service interactions.

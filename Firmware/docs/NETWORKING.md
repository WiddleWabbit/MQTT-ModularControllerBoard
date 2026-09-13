# Networking services

## WiFi

`WifiManager` owns the states `Idle`, `Connecting`, `Connected`, and `Backoff`.
`begin()` starts one station attempt. `update()` observes the injected
`IWifi`, times out unsuccessful attempts, and retries with capped exponential
backoff. It never delays or waits for a connection.

`IWifi` exposes `begin`, `status`, and `disconnect`. The ESP32 implementation
is `Esp32Wifi`; native tests use `FakeWifi`, which records credentials and
disconnects and allows each link state to be selected.

## NTP

`NtpService` configures the injected `INtpAdapter` and reports `Idle`,
`WaitingForSync`, or `Synchronized`. It periodically reapplies configuration
while waiting, then exposes the synchronized epoch through `currentTime()`.
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

## Configuration and testing

Network credentials and broker settings are composed in `src/main.cpp`.
Credentials should be supplied in the deployment configuration rather than
committed to source control. Native tests use injected `FakeClock` instances
to advance retry windows deterministically and verify all public service
operations and cross-service interactions.

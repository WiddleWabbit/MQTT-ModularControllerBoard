# MQTT

## Purpose

`MqttManager` maintains one MQTT session after WiFi is available. It retries
failed connections without blocking, restores configured subscriptions after
reconnection, publishes only while connected, and routes incoming messages to
the application callback.

## Public API

`MqttConfig` contains broker host/port, client credentials, and reconnect
interval. `MqttManager::update()` is called from the main loop. Applications
use `addSubscription()`, `publish()`, and `setMessageHandler()`. The manager
reports `WaitingForWifi`, `Backoff`, or `Connected`.

`IMqttClient` is the hardware-independent transport contract. Production code
uses `Esp32MqttClient`, which wraps `PubSubClient` over an injected Arduino
`Client`.

## Sequence and failure behaviour

The manager waits for WiFi, attempts an immediate connection, then waits the
configured interval after a failure. A successful connection subscribes to all
registered topic filters. Any subscription failure closes the session and
enters backoff. Losing WiFi closes MQTT immediately. `loop()` runs only while
connected.

## Configuration

Set broker values in `MqttConfig` and provide credentials from the project
configuration store in production. Do not commit broker passwords or other
secrets. The example composition in `src/main.cpp` uses `mqtt.local` as a
placeholder.

## Desktop testing

`test/test_desktop/fakes/FakeMqttClient.h` records every operation and allows
connection, publish, and subscription success/failure to be selected. It can
inject normal and binary callback payloads. `test_mqtt_manager.cpp` covers
WiFi gating, retries, reconnects, subscriptions, publish failures, routing,
and loop servicing with Unity under the native PlatformIO environment.

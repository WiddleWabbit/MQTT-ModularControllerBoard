# Network reference

[Home](../home.md) · Guide: [Network](../networking.md)

Source: `lib/Logic/Network/Network.h`, `WifiManager.h`, `NtpService.h`, `MqttService.h`, `MqttTopicLayout.h`, `NetworkRuntime.h`, `INetworkConfigStore.h`.

`main.cpp` does not call the classes below. Construction, `begin`, `apply`, and the handler fan-out are covered through `Network` in `test_deep_modules.cpp`. The state machines keep their own tests.

## Purpose

`Network::update()` advances `WifiManager`, then `NtpService`, then `MqttService`. Inbound payloads are delivered to every handler registered with `addMessageHandler`. At most four handlers fit (`kMaxInboundHandlers`). Sensor, solenoid, and pump register one each. Typed commands are decoded in those modules.

## Classes

`begin(defaults)` loads the stored record over the defaults, then starts Wi-Fi, MQTT, and NTP. It returns true when at least one network key was stored. `apply` persists the selected fields and pushes Wi-Fi or MQTT only after that save. `publish` forwards to `MqttService` and is rejected while disconnected. `config` returns the active record.

## States

### Wi-Fi

`WifiManager` owns `Idle`, `Connecting`, `Connected`, and `Backoff`. `begin()` starts one station attempt. `update()` observes `IWifi`, times out unsuccessful attempts, and retries with capped exponential backoff. It does not wait for a connection.

`IWifi` exposes `begin`, `status`, `rssi`, `localIp`, `setHostname`, `resetStationMode`, and `disconnect`. The ESP32 adapter is `Esp32Wifi`. `FakeWifi` records credentials, hostname commits, station-mode resets, and disconnects. `WifiManager::rssi()` and `localIp()` forward the driver values.

A non-empty hostname is committed only when it differs from the name already given to the driver. `WifiManager` calls `resetStationMode()` and then `setHostname()` before `begin()`. `Esp32Wifi` turns station mode off in `resetStationMode()` because Arduino-ESP32 2.0 commits the DHCP hostname only when station mode changes. A retry with the same name calls `begin()` only.

### NTP

`NtpService` reports `Idle`, `WaitingForSync`, or `Synchronized`. It reapplies configuration while waiting, then exposes the epoch through `currentTime()`. `currentTime()` returns 0 until synchronized. `config()` returns the servers, the UTC and daylight offsets, and the retry interval.

`Esp32NtpAdapter` calls Arduino `configTime` and treats an epoch at or after 1700000000 (November 2023) as valid. `FakeNtpAdapter` records servers and offsets and can toggle synchronization. Wall-clock formatting for the USB snapshot is in the [serial console reference](serial-console.md). NTP does not depend on the MQTT lookup.

`kNtpConfig` in `src/main.cpp` is `pool.ntp.org`, `time.nist.gov`, no third server, UTC offset 28800 seconds, daylight offset 0, retry 60000 ms. Field order is server1, server2, server3, utc offset, daylight offset, retry. The 60000 ms value is the retry, not the daylight offset.

### MQTT

`MqttService` owns `Idle`, `Unconfigured`, `WaitingForNetwork`, `Connecting`, `Connected`, and `Backoff`. An empty broker host stays `Unconfigured` and does not call `connect`, so the DNS resolver is not asked for a blank name. `update(networkReady)` otherwise waits for Wi-Fi, reconnects without blocking, subscribes after a successful connection, services the client, and disconnects when Wi-Fi is lost.

The service enters `Connected` only after every configured subscription succeeds. A subscription failure closes the broker connection and uses the normal reconnect backoff. `publish` is rejected while disconnected. Inbound payloads are forwarded through `MqttMessageCallback`.

`IMqttClient` is the client port. `PubSubClientAdapter` wraps an already-constructed `PubSubClient`. `FakeMqttClient` provides connection failures, subscription and publication results, and inbound delivery. The port contract is in [interfaces.md](../interfaces.md).

## Sequences

### Subscriptions

`MqttTopicLayout` builds the five QoS 1 subscriptions. They are not stored in NVS. `set mqtt.prefix` changes the root, and `NetworkRuntime` pushes the new list into `MqttService`.

| Topic | Who handles it |
| --- | --- |
| `{id}/solenoids` | Solenoid bridge |
| `{id}/solenoids/connected` | Solenoid bridge, same handler |
| `{id}/pump` | Pump bridge |
| `{id}/sensor/read` | Sensor bridge |
| `{id}/sensor/connected` | Sensor bridge, same handler |

`kSubscriptionCount` is 5, and all five are in use. A new command topic means raising that count. Command payloads are on the type reference pages.

`{id}` is one path segment. `kMqttDeviceId` in `src/main.cpp` supplies it when NVS has no `mqtt_prefix`. That constant is `watering`. The hostname and the MQTT client id stay independent. Two boards on one broker need different client ids as well as different prefixes. The character rules are in the [configuration reference](configuration.md).

`Network` is constructed with the client-id literal `watering-controller`. `begin()` replaces it from the loaded record before any connect.

### Generation

Publishers remember the payload the broker has accepted. Slot status, the solenoid inventory, and the sensor inventory compare that text. Sensor, solenoid, and pump state compare the poller's revision, so a repeated value is sent again only after a new sample is stored.

`MqttTopicLayout` keeps a generation counter. It starts at 1 and adds 1 only when the id text changes. Each publisher remembers the generation it last published under. The next `update()` sees a newer count, forgets the accepted-payload notes, and sends the current values once on the new topics. A note is stored again only after that publish is accepted, so a reconnect retries whatever the broker has not taken. Later passes with the same readings go quiet. Retained messages under the previous id are left on the broker.

Slot topics and the enumeration text are in the [module bus reference](module-bus.md).

## Errors

An empty `apply` does not reconnect. A failed save leaves the active config unchanged and keeps the staged fields dirty. Wi-Fi field changes restart Wi-Fi only. MQTT field changes restart MQTT only.

`Connecting` is a real `MqttService` state and is usually gone by the time `update()` returns, so the USB line seldom shows it.

## Configuration

`kNetworkTiming` in `src/main.cpp` is `{15000, 1000, 30000, 1000, 30000}`: Wi-Fi connect timeout, Wi-Fi initial retry, Wi-Fi max retry, MQTT initial retry, MQTT max retry. All are milliseconds.

Credentials belong in the deployment configuration, or in NVS after `apply`. They should not be committed as a filled-in record in source.

### `INetworkConfigStore`

This contract is not a hardware port and does not live in `lib/Interfaces/`. It is inside `Network`. `PreferenceNetworkConfigStore` adapts `IPreferenceStore`. `FakeNetworkConfigStore` is the other adapter, used by the configuration tests. Both adapters are why the contract exists.

- `load()` overlays stored fields onto the defaults already in the destination. Missing keys keep those defaults. A missing client id is stored. A missing `mqtt_prefix` keeps the supplied default and is not written. It returns false when no network key is stored.
- `save(config, fields)` writes only the selected fields. An empty string is a successful write when the key exists afterwards. `putString` of an empty string returns 0 on both success and failure, so success is "the key exists afterwards", not a non-zero return. Other stored fields stay unchanged.
- `loadWarning()` returns a boot note from the last load, or an empty string.
- Fakes must expose load and save results, the field mask, and owned stored values.

Stored keys and the `set` grammar are in the [configuration reference](configuration.md).

## Tests

- `test/test_desktop/test_networking.cpp` — Wi-Fi, NTP, and MQTT state machines, including backoff and an empty broker host.
- `test/test_desktop/test_mqtt_topics.cpp` — topic text and the generation counter.
- `test/test_desktop/test_configuration.cpp` — load, save, and `apply`.
- `test/test_desktop/test_deep_modules.cpp` — `Network::begin`, `apply`, and the handler fan-out.

Native tests use `FakeClock` to advance retry windows.

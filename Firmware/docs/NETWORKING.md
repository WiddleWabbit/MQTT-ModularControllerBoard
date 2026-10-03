# Networking services

`Network` is the module `loop()` calls. `Network::update()` advances
`WifiManager`, then `NtpService`, then `MqttService`. The classes below live
inside that module. `main.cpp` does not call them. Construction, `begin`,
`apply`, and the four-handler fan-out are covered with the module in
`test_deep_modules.cpp`. The state machines below keep their own tests.

## WiFi

`WifiManager` owns the states `Idle`, `Connecting`, `Connected`, and `Backoff`.
`begin()` starts one station attempt. `update()` observes the injected
`IWifi`, times out unsuccessful attempts, and retries with capped exponential
backoff. It never delays or waits for a connection.

`IWifi` exposes `begin`, `status`, `rssi`, `localIp`, `setHostname`,
`resetStationMode`, and `disconnect`. The ESP32 implementation is `Esp32Wifi`;
native tests use `FakeWifi`, which records credentials, hostname commits,
station-mode resets, and disconnects, allows each link state to be selected,
and exposes a settable RSSI and address. `WifiManager::rssi()` and
`localIp()` forward the driver values.

A non-empty hostname is committed only when it differs from the name already
given to the driver. `WifiManager` calls `resetStationMode()` and then
`setHostname()` before `begin()`. `Esp32Wifi` turns station mode off in
`resetStationMode()` because Arduino-ESP32 2.0 commits the DHCP hostname only
when station mode changes. A retry with the same name calls `begin()` only.

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

`{id}` in the topics below is the board's device id. It is one path segment.
`kMqttDeviceId` in `src/main.cpp` supplies it when NVS has no `mqtt_prefix`
key; that constant is `watering`, so an unset board still uses
`watering/slot/1` and `watering/solenoids`. `set mqtt.prefix` stores a
different id and restarts MQTT only. The hostname and the MQTT client id
stay independent. Two boards on one broker need different client ids as
well as different prefixes.

Publishers remember the payload the broker has already accepted. Slot status
and the solenoid inventory compare that text. Sensor, solenoid, and pump
state compare the poller's revision, so a repeated value is sent again only
after a new sample is stored. `MqttTopicLayout` keeps a generation counter,
starting at 1, and adds 1 only when the id text changes. Each publisher
remembers the generation it last published under. The next `update()` sees
a newer count, forgets those accepted-payload notes, and sends the current
values once on the new topics. A note is stored again only after that
publish is accepted, so a reconnect retries whatever the broker has not
taken. Later passes with the same readings go quiet. Retained messages
already stored under the previous id are left on the broker. The serial
command, the character rules, and a commissioning example are in
[CONFIGURATION.md](CONFIGURATION.md).

`ModuleSlotPublisher` reads `ModuleHost` public snapshots and publishes one
retained message per slot when that text changes. `ModuleBus::update()`
calls it after `ModuleHost::update()`. The host does not depend on MQTT, and
the publisher does not call `ping()` or `echo()`. Topics are `{id}/slot/1`
through `{id}/slot/4` (firmware index 0 is topic 1). Payloads use the
same words as the serial slot line, without the `Slot N:` prefix:

```text
Empty
Debouncing
Enumerating
Online IdentityEcho addr=0x10
Unsupported type=0x02AA addr=0x12
Fault Nack
```

Publication waits until `MqttService` is connected. A rejected publish stays
pending and is retried on a later `update()`. An unchanged snapshot is not
sent again, including after a broker reconnect. A new device id is the
exception: the generation change makes the current text count as unpublished,
so each slot is sent once under the new prefix. The publish contract has no
QoS argument, so slot status uses the client default.

`SensorPoller` reads an online Sensor module (`0x0200`) without going through
MQTT. The first query after identify, including after the module restarts and
is identified again, is the input count. Presence and a raw int32 reading for
every input then run immediately, and again every `kSensorPollIntervalMs`
(60 seconds, set in `src/main.cpp`). One sensor query runs per
`SensorModule::update()`, after `moduleBus.update()`.

`SensorMqttBridge` publishes a retained reading at `{id}/slot/N/sensor/M`
(module slot and sensor number are both 1-based) each time a poll or an
immediate read stores a sample, including when the value is unchanged.
Another `update()` with no new sample does not publish again. Payloads are
`connected <value>`, `disconnected`, or retained `unavailable` when that
input is gone. `{id}/sensor/read` with payload `N M` asks for sensor M
on module slot N immediately. The callback enqueues the read; the next
poller update performs it and the bridge publishes that result. A failed
immediate read publishes non-retained `unavailable` and leaves the last
retained reading in place. Malformed payloads are ignored. The firmware
subscribes to `{id}/sensor/read` at QoS 1 beside the existing command
topics. Command parsing, poll order, and the publish rules are in
[SENSORMODULE.md](SENSORMODULE.md).

`SolenoidPoller` reads an online Solenoid module (`0x0100`) the same way.
The first query after identify, including after the module restarts and is
identified again, is the output count. Each output's state (`on`, `off`, or
`disconnected`) is then read immediately, and again every 60 seconds. One
solenoid query runs per `SolenoidModule::update()`, after the sensor module.
`SolenoidMqttBridge` publishes a retained state at
`{id}/slot/N/solenoid/M` each time a read or a set stores a state.
`{id}/solenoids` with payload `N on off ...` is the desired state of
every output on module slot N. The callback records it. `{id}/solenoids/connected`
with payload `N` asks for the output list and does not record desired state
or restart the silence window. Once every output state is known, the bridge
publishes retained `{id}/slot/N/solenoids` as the count followed by the
connected indexes (`4 1 2 4`). Later poller passes
send `SET_SOLENOID` only for outputs that are not already in that state.
`kSolenoidCommandTimeoutMs` in `src/main.cpp` is 15 minutes. That long
without an accepted command turns every solenoid output off. The poll
interval beside it is `kSolenoidPollIntervalMs` (60 seconds). Command
parsing, which outputs are skipped, and the cutoff are in
[SOLENOIDMODULE.md](SOLENOIDMODULE.md).

`PumpPoller` reads an online Pump module (`0x0300`) the same way. The first
query after identify, including after the module restarts and is identified
again, is the pump state (`on`, `off`, or `fault`). That state is read again
every 60 seconds. One pump query runs per `PumpModule::update()`, after the
solenoid module. `PumpMqttBridge` publishes a retained state at
`{id}/slot/N/pump` each time a read, set, or reset stores a state.
`{id}/pump` with payload `N on` or `N off` is the desired state.
`N reset` resets the pump and does not, by itself, turn it on. The callback
records the request. Later poller passes send `SET_PUMP` only when the known
state differs, and they do not send it while the pump is faulted.
`kPumpCommandTimeoutMs` in `src/main.cpp` is 3 minutes. That long without an
accepted on/off command turns a pump that is on off. A reset does not
refresh that window. The poll interval beside it is `kPumpPollIntervalMs`
(60 seconds). Command parsing and the cutoff are in
[PUMPMODULE.md](PUMPMODULE.md).

`IMqttClient` is the narrow client contract. `PubSubClientAdapter` wraps an
already-constructed PubSubClient and is configured with `setServer`.
`FakeMqttClient` provides deterministic connection failures, subscription and
publication results, loop counts, and inbound callback delivery.

## Serial status

`SerialStatusReporter` writes WiFi, NTP, MQTT, and four slot lines through
`ISerialPort` on a configured snapshot interval while USB serial is plugged in
and periodic reporting is enabled. `begin(config)` starts reporting for the
power-on session; `reconfigure(config)` replaces the interval without stopping.
The interval stays in session RAM. Whether periodic reporting is enabled is
loaded from NVS (`status_report`) during `setup()` and changes only after
`set status on` or `set status off` is applied. `setup()` starts reporting at
10000 ms.

`status` prints one snapshot immediately, including when periodic reporting is
off, and restarts the interval. It still requires the USB link to be plugged in.

Connected WiFi includes the station address and RSSI. Address `0.0.0.0` keeps
the RSSI-only line. Other states omit both. MQTT labels match
`MqttServiceState` (`Idle`, `Unconfigured`, `WaitingForNetwork`, `Connecting`,
`Connected`, `Backoff`):

```text
WiFi Status: Connected (192.168.1.42, -62 dBm)
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

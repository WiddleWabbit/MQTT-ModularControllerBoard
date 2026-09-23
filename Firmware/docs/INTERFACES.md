# Hardware interfaces

The networking logic depends only on the abstract contracts in
`lib/Interfaces/`. Drivers implement these contracts for ESP32/Arduino, while
desktop tests use controllable fakes.

## `IClock`

- `millis()` returns a monotonic millisecond counter.
- Values may wrap at the counter width; consumers must use unsigned elapsed-time
  arithmetic.
- The fake clock supports deterministic advancement and wraparound tests.

## `IWifi`

- `begin(ssid, password)` starts one nonblocking station attempt.
- `status()` reports `Disconnected`, `Connecting`, or `Connected`.
- `rssi()` reports station signal strength in dBm.
- `localIp()` reports the station IPv4 address, or `0.0.0.0` when none is
  assigned.
- `setHostname(hostname)` stores the DHCP hostname committed on the next
  station start.
- `resetStationMode()` stops station mode so the next start commits that name.
  The ESP32 driver uses this because Arduino-ESP32 2.0 applies the hostname
  only when station mode changes.
- `disconnect()` stops the current attempt or connection.
- The driver must not wait for association inside `begin()`.
- The fake records credentials, hostname commits, mode resets, and
  disconnects, can return a sequence of link states, and exposes a settable
  RSSI and address.

## `INtpAdapter`

- `configure(server1, server2, server3, utcOffset, daylightOffset)` starts or
  refreshes platform SNTP configuration.
- `isSynchronized()` reports whether a valid epoch is available.
- `currentTime()` returns the synchronized Unix epoch, or the platform value
  when synchronization is not yet available.
- Configuration is asynchronous; the adapter must not block waiting for NTP.
- The fake records all configuration values and exposes synchronization and
  epoch controls.

## `IMqttClient`

- `setCallback()` installs the inbound payload callback and opaque context.
- `connect()` performs one broker attempt and returns its result.
- `connected()` reports the current broker state.
- `disconnect()` closes the broker session.
- `subscribe()` returns whether a topic subscription was accepted.
- `publish()` returns whether a publication was accepted.
- `loop()` services the underlying MQTT transport.
- The fake records all calls, supports result sequences, and can inject
  inbound messages.

The production adapters are intentionally thin: `Esp32Wifi` uses `WiFi`,
`Esp32NtpAdapter` uses `configTime()` and the system epoch, `Esp32Clock` uses
Arduino `millis()`, and `PubSubClientAdapter` wraps an existing
`PubSubClient`.

## `INetworkConfigStore`

- `load()` overlays stored fields onto the defaults already in the destination.
  Missing keys keep those defaults. It returns false when no network key is
  stored.
- `save(config, fields)` writes only the selected fields. An empty string is a
  successful write when the key exists afterwards. Other stored fields stay
  unchanged.
- `loadWarning()` returns a boot note from the last load, or an empty string.
- Fakes must expose load/save results, the field mask, and owned stored values.

## `IPreferenceStore`

- `open` / `close` bracket one read or write session.
- `contains` reports a key without reading it. Callers use it before
  `readString` so a missing key does not surface a storage error.
- `writeString` returns the number of characters stored. Empty text returns 0
  on both success and failure; `contains` distinguishes them.
- `writeUShort` returns 2 on success.

## `ISerialPort`

- `isPlugged()` is a nonblocking snapshot of the USB serial plug state.
- `available()` reports pending bytes, `read()` consumes one byte, and
  `writeLine()` emits one response line.
- The serial controller must not consume or emit bytes while the USB serial
  link is unplugged.
- Fakes must provide queued input and inspectable output.

## `IDigitalPin`

- `setMode()` selects `DigitalInput`, `DigitalInputPullup`, `DigitalOutput`,
  or `DigitalOutputOpenDrain`. Names avoid Arduino `INPUT`/`OUTPUT` macros.
- `DigitalOutputOpenDrain` drives LOW as the safe default after mode change.
- `read()` is true when the pin is HIGH. Sense present is LOW.
- `write()` drives an output; true is HIGH / open-drain Hi-Z.
- The ESP32 driver must use Arduino `pinMode` so UART0 detaches from GPIO43/44.
- Fakes must record mode, writes, push-pull HIGH attempts, and an external
  level override for sense.

## `I2cMaster`

- `begin()`, `setClockHz()`, and `setTimeoutMs()` configure the bus.
- `write()` issues STOP. `writeRead()` uses a repeated start; a write-phase
  NACK/timeout/bus-error returns immediately without the read phase.
- `recover()` clocks SCL up to nine times, issues STOP, and re-inits. It is
  best-effort; a still-stuck SDA needs the module unplugged.
- Results are `Ok`, `Nack`, `Timeout`, or `BusError`.
- Fakes record operations (address, tx, rxLen, STOP) and dispatch to
  simulated modules. They must support stuck-SDA and two-ACK collision.

Module command IDs, frame layout, and CRC-8/SMBus live in copyable
`lib/Interfaces/ModuleProtocol.h`. See `docs/MODULES.md`.

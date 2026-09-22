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
- `disconnect()` stops the current attempt or connection.
- The driver must not wait for association inside `begin()`.
- The fake records credentials and disconnects, can return a sequence of
  link states, and exposes a settable RSSI.

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

# Hardware interfaces

[Home](home.md)

Hardware ports live in `lib/Interfaces/`. Drivers implement them for
ESP32/Arduino. Desktop tests use controllable fakes. A port is a seam
because both adapters exist. Logic-to-logic interfaces are not added here.

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

This contract is not a hardware port and does not live in `lib/Interfaces/`.
It is inside `Network`. The load and save rules are in the
[network reference](reference/networking.md).

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

## `IBytePort`

- Raw bytes for programming traffic. Line framing stays on `ISerialPort`.
- `isPlugged()`, `available()`, and `read()` match the serial port.
- `write(data, length)` sends a buffer. A null pointer or a zero length
  writes nothing.
- The ESP32 adapter is the same USB CDC object as `ISerialPort`.
- Fakes queue input and record every written byte.

## `ISpiMaster`

- `begin(clockHz)` starts mode 0, MSB first, with SCK idle low.
- `end()` releases the bus.
- `transfer()` clocks one byte and returns the byte shifted in.
- The programmer calls `begin()` before it drives reset low, and it keeps
  the clock at 125 kHz.
- Fakes record the clock, the order `begin()` was called, and every byte,
  and they return a scripted MISO stream.

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
- `write()` issues STOP. `writeRead()` uses a repeated start. A NACK,
  timeout, or bus error is returned to the caller. The ESP32 driver does
  not print a NACK. The console reports `Slot N: Nack` once.
- `recover()` clocks SCL up to nine times, issues STOP, and re-inits. It is
  best-effort; a still-stuck SDA needs the module unplugged.
- Results are `Ok`, `Nack`, `Timeout`, or `BusError`.
- Fakes record operations (address, tx, rxLen, STOP) and dispatch to
  simulated modules. They must support stuck-SDA and two-ACK collision.

Module command IDs, frame layout, and CRC-8/SMBus live in copyable
`lib/Interfaces/ModuleProtocol.h`. Behaviour a daughter board must follow
is in [the daughter contract](daughter/contract.md).

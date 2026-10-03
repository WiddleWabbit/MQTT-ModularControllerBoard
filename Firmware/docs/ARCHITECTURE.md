# Firmware architecture

The controller is seven deep modules. Each one has a small interface and
hides its state machines. `src/main.cpp` is the composition root: it
constructs the ESP32 adapters, passes timing constants and `kMqttDeviceId`,
and calls `begin` / `update`. It does not sequence the steps inside a module.

Logic depends on hardware ports in `lib/Interfaces/`. ESP32 adapters live in
`lib/Drivers/`. Logic does not include Arduino headers.

## Modules

| Module | Interface | What one `update()` does |
| --- | --- | --- |
| `Network` | `begin`, `update`, `publish`, `addMessageHandler`, `apply`, `config` | Wi-Fi, then NTP, then MQTT |
| `SerialConsole` | `begin`, `update`, `updateStatus` | `update` reads USB lines. `updateStatus` prints the snapshot after the modules |
| `ModuleBus` | `begin`, `update`, `quiesce`, `resume` | At most one I2C transaction, then slot status if the text changed |
| `SensorModule` | `update` | At most one sensor exchange, then publish what it stored |
| `SolenoidModule` | `update` | At most one solenoid exchange, then publish what it stored |
| `PumpModule` | `update` | At most one pump exchange, then publish what it stored |
| `Programming` | `begin`, `update`, `active` | STK500 until idle timeout or a confirmed unplug |

`kMqttDeviceId` in `src/main.cpp` is the MQTT topic root used when
`mqtt_prefix` is not stored. The layout does not invent that id. `{id}` in
the topic names below is that root. Setting it is described in
[CONFIGURATION.md](CONFIGURATION.md).

Poll intervals and command-absence cutoffs are named constants in
`src/main.cpp` and are passed into the type modules. Sensor, solenoid, and
pump polls are 60 seconds. A solenoid command absent for 15 minutes turns
every output off. A pump on/off command absent for 3 minutes turns the pump
off. A pump reset does not refresh that window.

## Seams

A seam is a contract with two adapters. Hardware ports (`IClock`, `IWifi`,
`INtpAdapter`, `IMqttClient`, `IPreferenceStore`, `ISerialPort`, `IBytePort`,
`IDigitalPin`, `ISpiMaster`, `I2cMaster`) have an ESP32 driver and a desktop
fake. `INetworkConfigStore` lives in `lib/Logic/Network/` because
`PreferenceNetworkConfigStore` and `FakeNetworkConfigStore` are both real
adapters. It is not a hardware port.

Do not add an interface so one logic class can be replaced by a fake.
`WifiManager`, `NtpService`, `MqttService`, `ModuleHost`, and the pollers
stay as submodules. Their existing tests stay, because those tests cover
edges the module tests do not replace.

Sibling modules reach inside another module through a private accessor
(`Network::mqtt()`, `ModuleBus::host()`, and the same pattern). `main.cpp`
does not call those accessors.

## Folders

- `lib/Interfaces/` contains the hardware ports and `ModuleProtocol.h`.
- `lib/Drivers/` contains the ESP32 adapters.
- `lib/Logic/` is one PlatformIO library. Sources live in `Network`,
  `Console`, `Bus`, `Sensor`, `Solenoid`, `Pump`, and `Programming`.
- `test/test_desktop/fakes/` contains the desktop adapters.
- `test/test_desktop/` contains Unity tests. `test_deep_modules.cpp` drives
  the seven modules. The other files drive the submodules.
- `src/main.cpp` composes the product.

`platformio.ini` lists `lib_deps = Logic` on both environments. The library
finder does not compile sources that live in subfolders unless the library
is named. The `-I lib/Logic/...` flags only expose the headers. Leave
`lib_deps` in place.

## Loop

`loop()` always calls `network.update()`. Inbound MQTT commands are queued
there. While `programming.active()` is true, the same pass calls
`programming.update()` and returns. Wi-Fi, NTP, and MQTT keep running. The
console, the bus, and the type modules do not. When the session ends on that
pass, the latch is cleared, `startController()` runs if setup skipped it,
and the pass continues into the normal work.

Otherwise the pass is:

```text
serialConsole.update()
moduleBus.update()
sensorModule.update()
solenoidModule.update()
pumpModule.update()
serialConsole.updateStatus()
```

`program` inside `serialConsole.update()` stores the RTC latch, calls
`programming.begin()`, and returns before the bus. An immediate `status`
prints inside `update()`, before the modules. The periodic snapshot is
`updateStatus()`, after them, so it matches this pass. Heap and PSRAM lines
stay in `main.cpp` and print when USB is plugged in.

`update()` methods never wait for a network operation. Wi-Fi and MQTT retries
use wrap-safe elapsed-time checks and exponential backoff. I2C transactions
are bounded by a 50 ms driver timeout.

## What the modules hide

`Network` owns `WifiManager`, `NtpService`, `MqttService`, `MqttTopicLayout`,
and `NetworkRuntime`. `begin` loads the stored record over the defaults from
`main.cpp`, then starts Wi-Fi, MQTT, and NTP. `apply` persists the selected
fields and pushes Wi-Fi or MQTT only after that save. An empty broker host
stays unconfigured and does not call `connect`. Inbound payloads fan out to
at most four handlers. The type modules register in their constructors.

`SerialConsole` owns the command controller and the status reporter. The
on/off flag and the DHCP hostname are staged with the other `set` commands
and stored on `apply`. `status` prints one snapshot immediately, including
when periodic reporting is off. USB plug state gates input and the periodic
snapshot. `Esp32SerialPort` reads the ESP32 USB CDC plug state.

`ModuleBus` owns `ModuleHost` and `ModuleSlotPublisher`. The host configures
per-slot sense (input pull-up, LOW = present), MOD (open-drain enumeration
select), and CS (idle pull-up). Unconfigured modules share address `0x0A`.
The host selects one slot at a time with MOD, assigns `0x10 + slot`, and
identifies the type. At most one I2C protocol transaction runs per
`update()`. The publisher writes a retained snapshot when a slot's public
status text changes. It does not call `ping()` or `echo()`. Several modules
plugged in together are described in [MODULES.md](MODULES.md).

A Sensor module (`0x0200`) asks for the input count when identified, then
presence and a raw reading for each input on the poll interval. The bridge
publishes every stored reading, including a repeated value, and accepts
`{id}/sensor/read` as an immediate read. The MQTT callback only enqueues the
request. Behaviour is in [SENSORMODULE.md](SENSORMODULE.md).

A Solenoid module (`0x0100`) asks for the output count when identified, then
the on/off/disconnected state of each output. `{id}/solenoids` names the
desired state. The module sends on or off only where the module's state
differs. The absence cutoff turns every output off. The retained list of
connected outputs is `{id}/slot/N/solenoids`. Behaviour is in
[SOLENOIDMODULE.md](SOLENOIDMODULE.md).

A Pump module (`0x0300`) reads the pump when identified and again on the poll
interval. The bridge publishes `on`, `off`, or `fault`. `{id}/pump` names
the desired on/off state, or asks for a reset. The module sends on or off
only when the known state differs, and it resets the pump only after that
reset command. A reset does not refresh the absence window. Behaviour is in
[PUMPMODULE.md](PUMPMODULE.md).

`program` or `program isp` on the USB console starts an Arduino-as-ISP
session on firmware slot 1. `Programming::begin` quiesces the bus.
`IspProgrammer` speaks the STK500v1 subset avrdude uses. Desired solenoid
and pump commands wait until the session ends. Their absence windows keep
counting, so a session longer than the cutoff turns those outputs off on
the next module update. The idle timeout is `kProgrammingIdleTimeoutMs`
(60 seconds with no STK500 byte). After the USB link has been seen in that
session, `kProgrammingUnplugTimeoutMs` (1 second) of absent frames ends it
too. A missing frame before the port has been seen does not. An RTC
slow-memory marker in `.rtc_noinit` brings the session back after the
USB-open restart and is cleared when the session ends. Pins and the reset
sequence are in [PROGRAMMING.md](PROGRAMMING.md).

## Desktop testing

Tests run on the desktop with PlatformIO's `native` environment:

```text
C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native
```

`test_deep_modules.cpp` drives each module through its public interface:
connect and backoff, apply and persistence, empty-broker refusal, slot
publish and enumeration, prefix changes, sensor and solenoid and pump
commands, absence windows, programming quiesce, and the serial console.
The older files keep the submodule edges: retry sequences, frame codecs,
enumeration faults, poller cycles, and the STK500 command set.

The fakes simulate link state, RSSI, time, broker outcomes, subscriptions,
publications, inbound messages, persisted settings, USB presence, serial
bytes, GPIO levels, and I2C slaves. The RTC programming marker is ESP32-only
and is not in these tests. Production builds use `lib/Drivers/`. Test code
and fakes are not included.

# Serial console reference

[Home](../home.md) · Guide: [Serial console](../serial-console.md)

Source: `lib/Logic/Console/SerialConsole.h`, `SerialConfigController.h`, `SerialStatusReporter.h`.

## Purpose

`update()` reads complete USB lines and dispatches them. `updateStatus()` prints the periodic snapshot after the modules have advanced. `status` calls `printStatus()` inside `update()`, so that snapshot is from before this pass's bus and type modules.

The command grammar is in the [configuration reference](configuration.md). `program` is in the [programming reference](programming.md).

The console subscribes to nothing and publishes nothing. `MQTT Status:` on the snapshot is the client state, printed on USB. The broker topics are in the [MQTT reference](mqtt.md).

## Classes

`SerialConsole::begin()` starts the reporter at `kSerialStatusIntervalMs` and copies the active `status_report` flag into it. Call it after `Network::begin()`.

`SerialStatusReporter::update()` writes the snapshot when it has been started, periodic reporting is enabled, USB is plugged in, and the interval has elapsed. `printStatus()` writes one snapshot immediately when USB is plugged in, including while periodic reporting is off. A successful immediate print sets the last-report time to now, so the periodic interval starts again from that print.

`setReportingEnabled(true)` also restarts the interval from now. `reconfigure` replaces the interval for the rest of the boot. No USB command calls it. The product interval is the value passed to `begin`.

The reporter reads `WifiManager`, `NtpService`, `MqttService`, and `ModuleHost` snapshots. It must not call `ping()` or `echo()`.

## Snapshot

Seven lines. Connected Wi-Fi with an address other than `0.0.0.0` includes the address and RSSI. Address `0.0.0.0` keeps the RSSI-only line. Other Wi-Fi states omit both. MQTT labels are the `MqttServiceState` names. Unsynchronized NTP omits the time.

```text
WiFi Status: Connected (192.168.1.42, -62 dBm)
NTP Status: Synchronized (2026-09-21 16:04:00)
MQTT Status: Connected
Slot 1: Empty
Slot 2: Online IdentityEcho addr=0x11
Slot 3: Unsupported type=0x02AA addr=0x12
Slot 4: Fault Nack
```

Wi-Fi forms:

```text
WiFi Status: Connected (192.168.1.42, -62 dBm)
WiFi Status: Connected (-62 dBm)
WiFi Status: Connecting
```

NTP forms:

```text
NTP Status: Synchronized (YYYY-MM-DD HH:MM:SS)
NTP Status: WaitingForSync
```

Local time is the UTC epoch plus `NtpConfig` `utcOffsetSeconds` and `daylightOffsetSeconds`, formatted 24-hour. Logic does not call Arduino `localtime()`. Time is omitted unless the service is `Synchronized`.

`Connecting` is a real MQTT state and is usually not visible after `MqttService::update()` returns. The other labels are `Idle`, `Unconfigured`, `WaitingForNetwork`, `Connected`, and `Backoff`.

Firmware slot 1 is index 0 and address `0x10`. Slot text is the same body `ModuleSlotPublisher` uses, with a `Slot N:` prefix. The body itself is in the [module bus reference](module-bus.md).

Heap and PSRAM lines are printed from `loop()` in `main.cpp` every `kMemoryReportIntervalMs` (30 seconds) when USB is plugged in. They are not part of this snapshot, and they are not printed while a programming session is active.

## Sequences

Input and both prints are suppressed when `ISerialPort::isPlugged()` is false. On the ESP32 that is the USB CDC plug state, which tracks host start-of-frame, not an open COM application.

`status` does not change the on/off flag and does not change the interval length. `set status on` and `set status off` are staged. They take effect on `apply`, which stores `status_report` as 1 or 0 in the network namespace and then calls `setReportingEnabled`. A missing key leaves reporting on and is not written until applied. `status` still prints while a status change is staged and not yet applied.

The interval stays in session RAM. Reboot restores `kSerialStatusIntervalMs` (10 seconds) from `setup()` via `begin`. No command stores a different interval.

## Errors

Lines that are not a known command are rejected by `SerialConfigController`. The reply strings (`ERR hostname`, `ERR prefix`, `ERR status`, `ERR program`, `OK applied`) are in the [configuration reference](configuration.md).

`printStatus()` writes nothing when USB is unplugged.

A slot in `Fault Nack` prints `Slot N: Nack` once from `updateStatus()`, including while periodic reporting is off. The following snapshot still contains `Slot N: Fault Nack`. The short line is not printed again until the slot has been `Empty`, `Online`, or `Unsupported`. A snapshot that already includes the fault counts as the one report, so the short line is skipped on that pass. USB unplugged prints neither. An I2C NACK is not printed by the driver.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kSerialStatusIntervalMs` | 10 seconds | Periodic snapshot. Not stored |
| `kMemoryReportIntervalMs` | 30 seconds | Heap and PSRAM lines in `loop()` |

## Tests

`test/test_desktop/test_status.cpp` pins the epoch and offsets and covers plug gating, interval spacing, `begin`, `reconfigure`, MQTT state labels, slot lines, and time advancing after sync. `test_configuration.cpp` covers `set`, `apply`, `status`, and `program`. `test_deep_modules.cpp` drives the console through `SerialConsole`.

Snapshots in tests are seven lines. The reporter is constructed with an empty module host when the test has no bus of its own.

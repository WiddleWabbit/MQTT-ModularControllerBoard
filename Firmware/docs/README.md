# Firmware Documentation

This directory documents the firmware boundaries, runtime workflows, and
desktop test strategy.

* [Architecture](ARCHITECTURE.md) - layer responsibilities and dependency
  direction.
* [Hardware interfaces and fakes](INTERFACES.md) - interface contracts and
  the behavior expected from test doubles.
* [WiFi setup and connection](WIFI.md) - credential setup, captive portal, and
  station reconnection workflows.
* [NTP synchronization](NTP.md) - connection-aware time synchronization and
  scheduling.
* [Serial console](SERIAL.md) - injected console output and its desktop fake.

The public C++ APIs are documented at their declarations in
`lib/Interfaces/`, `lib/Drivers/`, `lib/Logic/`, and `src/class/`. Desktop
tests use the fakes in `test/test_desktop/fakes/` and should be run from the
`Firmware/` directory with:

```text
pio test -e native
```

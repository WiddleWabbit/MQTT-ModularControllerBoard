# Firmware Architecture

## Overview

The firmware is organized as three layers:

* **Interfaces** (`lib/Interfaces/`) define the smallest contracts for time,
  WiFi, credential storage, scanning, NTP, and system control.
* **Drivers** (`lib/Drivers/`) adapt those contracts to ESP32/Arduino APIs.
* **Logic** (`lib/Logic/` and `src/class/`) implements state machines and
  application workflows without directly owning hardware APIs where a driver
  can be injected.

`src/main.cpp` composes the production drivers and calls the non-blocking
services from Arduino `setup()` and `loop()`.

## Dependency direction

Logic depends on interfaces, never on concrete ESP32 drivers.  This keeps
connection timeouts, credential validation, portal rendering, and NTP
scheduling deterministic and desktop-testable.  `WiFiHandler` is the
application-facing coordinator; it supplies default ESP32 drivers when callers
do not inject alternatives.

## Desktop testing

The `native` PlatformIO environment uses Unity and excludes the ESP32
environment's hardware-only test code.  Tests in `test/test_desktop/` inject
the controllable `Fake*` implementations from `test/test_desktop/fakes/`.
Fakes record calls and expose state/configuration for success, failure,
timeouts, and sequencing tests.

Run the complete desktop suite from `Firmware/`:

```text
pio test -e native
```

Production builds use `custom-esp32`; desktop fakes and tests are not included
in that environment.


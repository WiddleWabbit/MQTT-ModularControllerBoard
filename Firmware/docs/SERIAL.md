# Serial Console

## Purpose

The serial console provides startup, WiFi, and diagnostic output without
coupling application code to Arduino's global `Serial` object.

## Public API and responsibilities

* `ISerial` defines `begin`, `print`, `println`, and printf-style output,
  including common integer, boolean, and floating-point overloads.
* `Esp32Serial` adapts those calls to the ESP32 Arduino USB serial console.
* `FakeSerial` implements the same contract for native Unity tests.  It records
  initialization and writes, and exposes complete output as a string.
* `WiFiHandler` accepts an optional `ISerial*` after its existing dependencies,
  preserving existing constructor call sites while allowing injection.

`print` does not add a line ending. `println` adds one newline. Floating-point
output uses two decimal places, and a null string is treated as empty text.
Formatted output preserves call order and ignores a null format pointer.
`isConnected()` reports whether output is currently available.  Production
writes are best-effort: when the USB host is disconnected, they return zero
without waiting or buffering output.  `begin()` never waits for a USB host.

## Configuration and usage

Production composition creates one `Esp32Serial`, initializes it at 115200
baud in `setup()`, and passes it to `WiFiHandler`.  No Arduino headers are
needed by `ISerial` or `FakeSerial`, so native tests do not include hardware
dependencies.

## Testing

`test_serial.cpp` covers initialization, output ordering, formatted output,
all supported numeric categories, line endings, null and empty inputs, and the
interaction-visible write count. It also verifies disconnected output is
discarded without blocking, output resumes after a late connection, and output
stops and resumes across a disconnect/reconnect sequence. Run it with:

```text
pio test -e native
```

# Hardware Interfaces and Fakes

The interfaces in `lib/Interfaces/` are the hardware boundary.  Production
drivers implement them with Arduino/ESP32 APIs; desktop tests use the matching
fakes.

| Interface | Meaning | Fake capabilities |
| --- | --- | --- |
| `IClock` | Monotonic millisecond time for elapsed-time logic. | `FakeClock` advances time deterministically. |
| `IWifiStation` | Start, stop, and inspect station connectivity. | `FakeWifiStation` records credentials and controls connection state. |
| `IWifiScanner` | Fill a bounded array with nearby network results. | `FakeWifiScanner` supplies results or negative scan failures. |
| `IWifiCredentialsStore` | Initialize, load, save, and clear persistent credentials. | `FakeWifiCredentialsStore` controls availability and save failures and records calls. |
| `ISystemControl` | Request a system restart. | `FakeSystemControl` counts restart requests. |
| `INtpClient` | Start and advance asynchronous NTP synchronization, read Unix time, and apply timezone conversion. | `FakeNtpClient` records requests, controls synchronization, and supplies test times. |
| `ISerial` | Initialize the console and write ordered text, numeric, line-oriented, and printf-style output. | `FakeSerial` records baud rate, write count, and complete output for exact ordering assertions. |

Implementations must preserve the interface result meanings: negative scan
results represent failure, `load()` returns false when no usable SSID exists,
and setters/reporting methods must not block.  Fakes should remain inspectable
and controllable so tests can cover successful operations, failures,
timeouts, retry sequences, and state transitions.

## Serial contract

`ISerial::begin()` records the requested baud rate and `print()` appends
without a line ending.  `println()` appends exactly one `\n`; numeric overloads
use Arduino-compatible integer output and two decimal places for floating-point
values.  `printf()` uses standard printf formatting and ignores a null format
pointer.  The production `Esp32Serial` forwards these operations to Arduino
USB serial; `FakeSerial` performs the same formatting in memory and exposes its
output for desktop tests.

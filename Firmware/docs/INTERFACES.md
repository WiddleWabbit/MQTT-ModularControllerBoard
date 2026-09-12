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

Implementations must preserve the interface result meanings: negative scan
results represent failure, `load()` returns false when no usable SSID exists,
and setters/reporting methods must not block.  Fakes should remain inspectable
and controllable so tests can cover successful operations, failures,
timeouts, retry sequences, and state transitions.

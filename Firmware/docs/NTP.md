# NTP Synchronization

## Purpose

`NtpHandler` schedules asynchronous NTP requests only while station WiFi is
connected.  It requests an initial synchronization after connection, then
requests again when the configured interval expires.  Losing WiFi resets the
connection edge so reconnection triggers an immediate request.

## Public API and responsibilities

* `NtpHandler::update()` advances the workflow without blocking.
* `setServer()` accepts a non-empty host name.
* `setUpdateFrequencyMs()` accepts a non-zero interval.
* `server()`, `updateFrequencyMs()`, and `isSynchronized()` expose current
  configuration and status.
* `INtpClient` abstracts the asynchronous NTP implementation.
* `Esp32NtpClient` adapts the ESP32 SNTP APIs; `Esp32Clock` supplies elapsed
  milliseconds.

The default server is `pool.ntp.org` and the default interval is 30 minutes.
`NtpHandler` calls the injected client's `update()` on every connected update,
but never waits for synchronization.

## Testing

`FakeWifiStation`, `FakeClock`, and `FakeNtpClient` verify initial requests,
periodic requests, reconnection behavior, invalid configuration, update
forwarding, and synchronized status on the native desktop environment.


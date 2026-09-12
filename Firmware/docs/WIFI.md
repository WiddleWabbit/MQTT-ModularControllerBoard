# WiFi Setup and Connection

## Purpose

The WiFi subsystem first tries persisted station credentials.  If credentials
are unavailable, it scans for visible networks and starts a captive portal for
manual configuration.  Station connection attempts are non-blocking and retry
after a configurable timeout.

## Main classes

* `WiFiHandler` is the application-facing coordinator.  `begin()` chooses
  station or portal mode, and `update()` services the selected mode.
* `WifiSetupController` loads/saves bounded credentials, filters scan results,
  removes hidden networks, and keeps the strongest duplicate SSID.
* `WifiConnectionManager` owns the station connection state machine:
  `Idle`, `Connecting`, `Connected`, and `RestartRequested`.
* `WifiSetupPortalView` renders the form and HTML-escapes scanned SSIDs.
* `Esp32WifiStation`, `Esp32WifiScanner`, and
  `Esp32WifiCredentialsStore` are the production drivers.

## Important sequences

1. `WiFiHandler::begin()` calls `WifiSetupController::begin()`.
2. Stored credentials enter station mode; otherwise the controller scans and
   the handler starts the access point, DNS redirect, and web server.
3. A valid portal submission is persisted and the device restarts.
4. In station mode, repeated `update()` calls start attempts, detect success,
   retry after timeout, and optionally request a restart at the failure limit.

An empty or whitespace-only SSID is rejected.  Passwords may be empty for an
open network, but both fields are bounded by the constants in
`IWifiCredentialsStore`.

## Testing

`FakeWifiStation`, `FakeWifiScanner`, `FakeWifiCredentialsStore`,
`FakeSystemControl`, and `FakeClock` make connection, scanning, persistence,
timeouts, retry limits, and portal transitions testable without hardware.
`WifiSetupPortalView` is tested for network rendering and HTML escaping.


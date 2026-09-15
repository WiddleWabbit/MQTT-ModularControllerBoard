# Runtime network configuration

`NetworkRuntime` is the single application boundary for network settings. It
loads the last complete record from `Esp32NetworkConfigStore` (ESP32 NVS) and
applies WiFi credentials and MQTT endpoint/credentials through the existing
interfaces. It owns copies of all active strings, so staged serial edits cannot
alter the active configuration before `apply`. A failed save never changes the
active configuration.

`SerialConfigController` accepts complete newline-terminated commands while
USB VBUS is present:

```text
set wifi.ssid garden
set wifi.password secret
set mqtt.host broker.local
set mqtt.port 1883
set mqtt.client watering-controller
set mqtt.username user
set mqtt.password password
apply
```

`set` commands update a staging copy only. `apply` validates persistence and
then restarts the WiFi and MQTT state machines. Unknown keys and invalid ports
are rejected. Serial input and responses are suppressed when VBUS is absent.

Native Unity tests use `FakeNetworkConfigStore`, `FakeUsbVbus`, and
`FakeSerialPort` to verify staging, explicit apply, persistence failure, and
USB gating without hardware.

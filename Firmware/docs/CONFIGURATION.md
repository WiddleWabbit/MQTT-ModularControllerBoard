# Runtime network configuration

`NetworkRuntime` is the single application boundary for network settings. It
loads stored fields from NVS through `PreferenceNetworkConfigStore` and applies
WiFi credentials and MQTT endpoint/credentials through the existing interfaces.
It owns copies of all active strings, so staged serial edits cannot alter the
active configuration before `apply`. A failed save never changes the active
configuration.

`SerialConfigController` accepts complete newline-terminated commands while
the USB serial link is plugged in:

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

`set` commands update a staging copy only. `apply` writes just the fields set
since the previous successful `apply`, then restarts WiFi when a WiFi field
changed and MQTT when an MQTT field changed. A later `set wifi.password` leaves
the stored SSID and broker settings untouched. Setting a field to an empty
value stores that empty value. `apply` with nothing newly set replies
`OK applied` and does not reconnect. Unknown keys and invalid ports are
rejected. Serial input and responses are suppressed when the USB serial link
is unplugged.

On boot, a stored record that has no `mqtt_client` key uses the built-in
client id, prints `Config: mqtt client id is not stored; using <id>`, and
saves that id. An empty MQTT host leaves MQTT `Unconfigured` and skips the
broker DNS lookup.

Native Unity tests use `FakeNetworkConfigStore` and `FakeSerialPort` to verify
staging, explicit apply, persistence failure, and USB serial plug gating
without hardware.

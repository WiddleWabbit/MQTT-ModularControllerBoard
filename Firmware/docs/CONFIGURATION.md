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
set wifi.hostname plant-room
set status on
set status off
set mqtt.host broker.local
set mqtt.port 1883
set mqtt.client watering-controller
set mqtt.username user
set mqtt.password password
apply
status
```

`set` commands update a staging copy only. `apply` writes just the fields set
since the previous successful `apply`, then restarts WiFi when a WiFi field
changed and MQTT when an MQTT field changed. A later `set wifi.password` leaves
the stored SSID, hostname, status flag, and broker settings untouched. Setting
a credential or broker field to an empty value stores that empty value. `apply`
with nothing newly set replies `OK applied` and does not reconnect. Unknown
keys and invalid ports are rejected.

`set wifi.hostname` stages the DHCP name. A name must be 1–31 characters,
use only letters, digits, and hyphens, and start and end with a letter or
digit. Anything else replies `ERR hostname` and stages nothing. `apply` stores
`wifi_hostname` and reconnects WiFi with that name. The broker settings are
left alone; the MQTT session drops with WiFi and reconnects on its own. A
missing hostname key uses `watering-controller` from `setup()` and is not
written until an apply stages one. On boot the station advertises that default
instead of the chip MAC name.

`set status on` and `set status off` stage periodic USB status snapshots.
`set status maybe` replies `ERR status`. `apply` stores `status_report` as
`1` or `0` in the network NVS namespace and then enables or disables periodic
printing. A missing key leaves reporting on and is not written until applied.
`status`, which is not a `set` command, prints one live WiFi, NTP, and MQTT
snapshot immediately, including while periodic reporting is off and while a
status change is still staged.

Serial input and responses are suppressed when the USB serial link is
unplugged.

On boot, a stored record that has no `mqtt_client` key uses the built-in
client id, prints `Config: mqtt client id is not stored; using <id>`, and
saves that id. An empty MQTT host leaves MQTT `Unconfigured` and skips the
broker DNS lookup.

Native Unity tests use `FakeNetworkConfigStore` and `FakeSerialPort` to verify
staging, explicit apply, persistence failure, and USB serial plug gating
without hardware.

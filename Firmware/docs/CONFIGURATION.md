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
set mqtt.prefix plant-room
set mqtt.username user
set mqtt.password password
apply
status
program
program isp
```

`set` commands update a staging copy only. `apply` writes just the fields set
since the previous successful `apply`, then restarts WiFi when a WiFi field
changed and MQTT when an MQTT field changed. A later `set wifi.password` leaves
the stored SSID, hostname, status flag, prefix, and broker settings untouched. Setting
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

`set mqtt.prefix plant-room` stages the MQTT device id. This id is the first
segment of every topic this board publishes or subscribes to. It uses the
hostname rules: 1–31 characters, letters, digits, and hyphens, and it must
start and end with a letter or digit. A slash, space, `+`, `#`, empty
value, leading or trailing hyphen, or a longer name replies `ERR prefix`
and stages nothing. `apply` stores `mqtt_prefix` and restarts MQTT only.
Wi-Fi and the MQTT client id are left as they are.

The id used when `mqtt_prefix` is missing is `kMqttDeviceId` in
`src/main.cpp`. That constant is `watering`, and it is not written until an
apply stages a prefix. With the default, the topics stay:

```text
watering/slot/N
watering/slot/N/sensor/M
watering/slot/N/solenoid/M
watering/slot/N/solenoids
watering/slot/N/pump
watering/solenoids
watering/solenoids/connected
watering/pump
watering/sensor/read
```

`{id}` in the other docs means this segment. After
`set mqtt.prefix plant-room` and `apply`, the same tree starts with
`plant-room` instead of `watering`.

Publishers normally stay quiet once the broker has accepted a payload. Slot
status and the solenoid inventory compare the text. Sensor readings, solenoid
states, and pump state compare the poller's revision, so a repeated value is
published again only when a new sample is stored. Changing the prefix does
not change those notes, so the new topics would otherwise stay empty until
some later sample or status change.

`MqttTopicLayout` keeps a generation counter so the move is immediate. The
counter starts at 1. Applying a different id adds 1. Each publisher remembers
the counter value it last published under. On the next pass it sees the new
value, forgets the "already sent" notes, and publishes the current values
once:

```text
plant-room/slot/1                  Online Sensor addr=0x10
plant-room/slot/1/sensor/1         connected 2500
plant-room/slot/1/solenoid/1       on
plant-room/slot/1/solenoids        4 1 2 4
plant-room/slot/1/pump             off
```

It then stores the new counter. Later passes with the same readings go quiet
again. The "already sent" note for each item is written only after the broker
accepts that item. During the MQTT reconnect, `publish` fails and the note
stays clear, so the following passes retry that item. A slot with no sample
is still skipped. Commands on the previous prefix are ignored after the
change. Retained messages left under the old prefix stay on the broker; this
firmware does not delete them.

The hostname, the MQTT client id, and the prefix are three separate
settings. A DHCP rename does not move the topics, and a prefix change does
not rename the station. Two boards still need different client ids, or the
broker will drop one of them. When commissioning a board, set all three to
the same name:

```text
set wifi.hostname plant-room
set mqtt.client plant-room
set mqtt.prefix plant-room
apply
```

`set status on` and `set status off` stage periodic USB status snapshots.
`set status maybe` replies `ERR status`. `apply` stores `status_report` as
`1` or `0` in the network NVS namespace and then enables or disables periodic
printing. A missing key leaves reporting on and is not written until applied.
`status`, which is not a `set` command, prints one live WiFi, NTP, and MQTT
snapshot immediately, including while periodic reporting is off and while a
status change is still staged.

`program` and `program isp` reply `OK programming` and a one-line jumper
reminder, then hand the USB byte stream to the ISP session described in
[PROGRAMMING.md](PROGRAMMING.md). The console does not read further lines
until that session ends. `program updi` and any other `program ...` line
reply `ERR program`.

Serial input and responses are suppressed when the USB serial link is
unplugged.

On boot, a stored record that has no `mqtt_client` key uses the built-in
client id, prints `Config: mqtt client id is not stored; using <id>`, and
saves that id. A stored record with no `mqtt_prefix` key keeps
`kMqttDeviceId` from `src/main.cpp` and does not write that key. An empty
MQTT host leaves MQTT `Unconfigured` and skips the broker DNS lookup.

Native Unity tests use `FakeNetworkConfigStore` and `FakeSerialPort` to verify
staging, explicit apply, persistence failure, and USB serial plug gating
without hardware.

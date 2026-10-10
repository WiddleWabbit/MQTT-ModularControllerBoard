# Configuration reference

[Home](../home.md) · Guide: [Runtime configuration](../configuration.md)

Source: `lib/Logic/Network/NetworkRuntime.h`, `INetworkConfigStore.h`, `lib/Logic/Console/SerialConfigController.h`.

`NetworkRuntime` is the boundary for network settings. It loads stored fields through `PreferenceNetworkConfigStore` and applies Wi-Fi and MQTT through the services. It owns copies of the active strings, so a staged edit cannot change the active configuration before `apply`. A failed save never changes the active configuration, and the staged fields stay dirty for a retry.

The store contract is in the [network reference](networking.md).

## Commands

`SerialConfigController` accepts complete newline-terminated commands while USB is plugged in:

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
program 1 isp
program 3 updi
```

`set` updates a staging copy only. `apply` writes just the fields set since the previous successful `apply`, then restarts Wi-Fi when a Wi-Fi field changed and MQTT when an MQTT field changed. A later `set wifi.password` leaves the stored SSID, hostname, status flag, prefix, and broker settings untouched. An empty credential or broker field is a valid stored value. `apply` with nothing newly set replies `OK applied` and does not reconnect. Unknown keys and invalid ports are rejected.

Serial input and responses are suppressed when the USB link is unplugged.

## Fields

| Command | NVS key | Apply restarts | Missing key |
| --- | --- | --- | --- |
| `set wifi.ssid` | `wifi_ssid` | Wi-Fi | Empty string from the defaults in `main.cpp` |
| `set wifi.password` | `wifi_password` | Wi-Fi | Empty string from those defaults |
| `set wifi.hostname` | `wifi_hostname` | Wi-Fi | `watering-controller` from `setup()`, not written until applied |
| `set mqtt.host` | `mqtt_host` | MQTT | Empty string. An empty host stays `Unconfigured` |
| `set mqtt.port` | `mqtt_port` | MQTT | `1883` from the defaults in `main.cpp` |
| `set mqtt.client` | `mqtt_client` | MQTT | `watering-controller`. A stored record that lacks the key prints the warning and saves it |
| `set mqtt.prefix` | `mqtt_prefix` | MQTT | `kMqttDeviceId`, not written |
| `set mqtt.username` | `mqtt_user` | MQTT | Empty until set |
| `set mqtt.password` | `mqtt_password` | MQTT | Empty until set |
| `set status on\|off` | `status_report` | Neither radio | Missing leaves reporting on, not written |

A password-only apply keeps the stored hostname and does not recommit it when it is unchanged. A namespace with none of these keys makes `load` return false. `begin` then keeps the defaults from `main.cpp` and does not write `mqtt_client`. A missing key inside a record that does exist keeps the default in this table.

On boot, a stored record with no `mqtt_client` key uses the built-in client id, prints `Config: mqtt client id is not stored; using <id>`, and saves that id. The built-in id is `watering-controller`. A stored record with no `mqtt_prefix` key keeps `kMqttDeviceId` from `src/main.cpp` and does not write that key.

### Hostname

`set wifi.hostname` stages the DHCP name. A name must be 1–31 characters, use only letters, digits, and hyphens, and start and end with a letter or digit. Anything else replies `ERR hostname` and stages nothing. `apply` stores `wifi_hostname` and reconnects Wi-Fi with that name. The broker settings are left alone. The MQTT session drops with Wi-Fi and reconnects on its own. On boot the station advertises the default instead of the chip MAC name.

Arduino-ESP32 2.0.17 stores the DHCP hostname in a 32-byte buffer, so 31 characters is the maximum it will commit. The commit happens when station mode changes. That is why a hostname apply resets station mode. See the [network reference](networking.md).

### Prefix

`set mqtt.prefix plant-room` stages the device id. This id is the first segment of every topic this board publishes or subscribes to. It uses the hostname rules: 1–31 characters, letters, digits, and hyphens, and it must start and end with a letter or digit. A slash, space, `+`, `#`, empty value, leading or trailing hyphen, or a longer name replies `ERR prefix` and stages nothing. `apply` stores `mqtt_prefix` and restarts MQTT only. Wi-Fi and the MQTT client id are left as they are.

The id used when `mqtt_prefix` is missing is `kMqttDeviceId` in `src/main.cpp`. That constant is `watering`. With the default, the topics stay:

```text
watering/slot/N
watering/slot/N/sensor/M
watering/slot/N/sensors
watering/slot/N/solenoid/M
watering/slot/N/solenoids
watering/slot/N/pump
watering/solenoids
watering/solenoids/connected
watering/pump
watering/sensor/read
watering/sensor/connected
```

`{id}` in the other docs means this segment. After `set mqtt.prefix plant-room` and `apply`, the same tree starts with `plant-room`. The payload for each topic, and a line to publish when testing, are in the [MQTT reference](mqtt.md).

Publishers normally stay quiet once the broker has accepted a payload. Changing the prefix bumps the generation counter in `MqttTopicLayout`, forgets those notes, and publishes the current values once:

```text
plant-room/slot/1                  Online Sensor addr=0x10
plant-room/slot/1/sensor/1         connected 2500
plant-room/slot/1/sensors          2 1
plant-room/slot/1/solenoid/1       on
plant-room/slot/1/solenoids        4 1 2 4
plant-room/slot/1/pump             off
```

The note for each item is written only after the broker accepts that item. During the reconnect, `publish` fails and the note stays clear, so later passes retry. A slot with no sample is still skipped. Commands on the previous prefix are ignored after the change. Retained messages under the old prefix stay on the broker. This firmware does not delete them. The counter itself is described in the [network reference](networking.md).

The hostname, the MQTT client id, and the prefix are three separate settings. When commissioning a board, set all three to the same name:

```text
set wifi.hostname plant-room
set mqtt.client plant-room
set mqtt.prefix plant-room
apply
```

### Status and program

`set status on` and `set status off` stage periodic USB snapshots. `set status maybe` replies `ERR status`. `apply` stores `status_report` as `1` or `0` and then enables or disables periodic printing.

`status` is not a `set` command. It prints one snapshot immediately, including while periodic reporting is off and while a status change is still staged. The snapshot is the seven lines in the [serial console reference](serial-console.md). It does not change the on/off flag. A successful print restarts the wait until the next periodic snapshot.

`program N isp` and `program N updi` reply `OK programming` and a one-line jumper reminder, then hand the USB byte stream to the session in [Programming](../programming.md). N is one digit, 1 through 4. The method word is exactly `isp` or `updi`. The console does not read further lines until that session ends. `program`, `program isp`, `program updi`, `program 1`, `program 1 udpi`, a slot outside 1..4, and any extra token reply `ERR program` and do not start a session. `programmable` is `ERR command`.

## Tests

`test/test_desktop/test_configuration.cpp` uses `FakeNetworkConfigStore` and `FakeSerialPort` for staging, apply, persistence failure, USB plug gating, hostname and prefix rejection, and `program`. `test_mqtt_topics.cpp` covers the generation move. `test_deep_modules.cpp` covers apply through `Network`.

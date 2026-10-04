# MQTT reference

[Home](../home.md) · Guide: [Network](../networking.md)

Source: `lib/Logic/Network/MqttTopicLayout.h`, `MqttService.h`. Payloads are built in `ModuleSlotPublisher`, `SlotStatusText`, `SensorMqttBridge`, `SolenoidMqttBridge`, and `PumpMqttBridge`.

This page is the command catalog. It lists every subscription and every publication, the payload text, and a line you can publish to test. When a command is applied, the poll order, and the absence windows stay on the module reference. If a page and the firmware disagree, the firmware wins. Update this page and that module page together.

`{id}` is the topic root. It is `kMqttDeviceId` in `src/main.cpp` (`watering`) until `set mqtt.prefix` is applied. The examples below use `watering`. Replace that segment when the prefix is something else.

## Purpose

`MqttTopicLayout` builds five QoS 1 subscriptions. `MqttService` subscribes after the broker connection succeeds, and it stays connected only when every subscribe succeeds. Sensor, solenoid, and pump each register one handler. The sensor handler serves both sensor topics. The solenoid handler serves both solenoid topics.

Outbound publishes are QoS 0. `IMqttClient::publish` has no QoS argument, and `PubSubClient`'s publish sets the retain bit only.

Publish a command with retain off. A retained command is delivered again each time the controller subscribes.

## Find a command

| You want                                   | Publish to                 | Payload                       | Result arrives on                              |
| ------------------------------------------ | -------------------------- | ----------------------------- | ---------------------------------------------- |
| One sensor reading                         | `{id}/sensor/read`         | `1 2`                         | `{id}/slot/1/sensor/2`                         |
| Which sensor inputs are connected          | `{id}/sensor/connected`    | `1`                           | `{id}/slot/1/sensors`                          |
| Desired state of every solenoid output     | `{id}/solenoids`           | `1 on on off off`             | `{id}/slot/1/solenoid/1` and the other outputs |
| Which solenoid outputs are connected       | `{id}/solenoids/connected` | `1`                           | `{id}/slot/1/solenoids`                        |
| Pump on, off, or reset                     | `{id}/pump`                | `1 on`                        | `{id}/slot/1/pump`                             |
| Whether a slot has a board, and which type | —                          | the controller publishes this | `{id}/slot/1` through `{id}/slot/4`            |

Slot numbers in every payload are 1..4. Slot 1 is I2C `0x10`, slot 2 is `0x11`, slot 3 is `0x12`, slot 4 is `0x13`. Sensor and solenoid indexes in topics and in connected-list results are 1-based.

To learn a count before naming every output, publish the connected-list command and read the first number of the result.

Watch the whole tree, then publish one command:

```text
mosquitto_sub -h <broker> -t watering/# -v
mosquitto_pub -h <broker> -t watering/sensor/read -q 1 -m "1 2"
```

`<broker>` is `mqtt.host`. The port default is `1883`. `mosquitto_pub` leaves retain off unless `-r` is passed. `-q 1` matches the subscription. The controller does not read the inbound QoS.

A new subscriber receives the last retained result from the broker. The controller sends that result again when a new sample is stored, or when the text changes, or when the command asks for the list again.

## Subscriptions

All five are QoS 1. They are not stored in NVS. `set mqtt.prefix` rebuilds the list under the new root.

| Topic | Payload | Handler |
| --- | --- | --- |
| `{id}/solenoids` | `N on off ...` | Solenoid bridge |
| `{id}/solenoids/connected` | `N` | Solenoid bridge, same handler |
| `{id}/pump` | `N on`, `N off`, or `N reset` | Pump bridge |
| `{id}/sensor/read` | `N M` | Sensor bridge |
| `{id}/sensor/connected` | `N` | Sensor bridge, same handler |

Words are lowercase. Separators are spaces or tabs. Leading and trailing spaces and tabs are accepted. A `+`, a sign, a decimal point, or an empty payload is ignored. An ignored command publishes nothing.

`N` is the slot, 1..4. `M` is the sensor, 1..16. `0` and a slot above 4 are ignored. A sensor number above 16 is ignored.

### `{id}/sensor/read`

Asks for one reading. `1 2` is slot 1, sensor 2.

```text
mosquitto_pub -h <broker> -t watering/sensor/read -q 1 -m "1 2"
```

The result is retained on `{id}/slot/N/sensor/M`:

```text
connected 2500
connected -4
disconnected
```

`connected` is followed by one raw int32. `disconnected` has no value.

A read that cannot be done publishes non-retained `unavailable` on that same topic. That leaves the previous retained reading on the broker. The request waits while the slot is still coming online or the count is not known yet. Up to four reads can wait. A fifth, while four are waiting, is dropped.

Behaviour: [sensor reference](sensor-module.md).

### `{id}/sensor/connected`

Asks for the connected-input list of one slot. The payload is the slot number alone.

```text
mosquitto_pub -h <broker> -t watering/sensor/connected -q 1 -m "1"
```

The result is retained on `{id}/slot/N/sensors`. The text is the count, then each connected index, 1-based, separated by a single space:

```text
2 1
0
4
4 1 2 4
```

`2 1` is two inputs, and input 1 is connected. `0` is a count of 0. `4` is four inputs, none connected. An extra token is ignored. The list is sent when the count and every presence are known. An unchanged list is sent again when this command asks.

Behaviour: [sensor reference](sensor-module.md).

### `{id}/solenoids`

Names the desired on or off state of every output on one slot. One lowercase word per reported output.

```text
mosquitto_pub -h <broker> -t watering/solenoids -q 1 -m "1 on on off off"
```

`1 on on off off` asks slot 1 for on, on, off, off. `SET_SOLENOID` is sent only where the known state differs. Each stored state is then retained on `{id}/slot/N/solenoid/M` as `on`, `off`, or `disconnected`.

The number of words must equal the count the module reported, or the states are not applied. A parsed command still restarts the absence window, including one whose length does not match. `ON`, a missing slot, and a slot outside 1..4 are ignored and do not restart the window.

`disconnected` is a result. It is not a word this command accepts.

If no accepted command arrives for `kSolenoidCommandTimeoutMs` (15 minutes), every output is turned off.

Behaviour: [solenoid reference](solenoid-module.md).

### `{id}/solenoids/connected`

Asks for the connected-output list of one slot. The payload is the slot number alone. It does not record a desired state, and it does not restart the absence window.

```text
mosquitto_pub -h <broker> -t watering/solenoids/connected -q 1 -m "1"
```

The result is retained on `{id}/slot/N/solenoids`, in the same shape as the sensor list. An index is listed when that output is `on` or `off`. A `disconnected` output is omitted. `4 1 2 4` is four outputs, with 1, 2, and 4 connected. An unchanged list is sent again when this command asks.

Behaviour: [solenoid reference](solenoid-module.md).

### `{id}/pump`

Names the desired state, or asks for a reset. The payload is the slot, a space, and one word.

```text
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 on"
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 off"
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 reset"
```

The result is retained on `{id}/slot/N/pump` as `on`, `off`, or `fault`.

`on` and `off` restart the absence window. `reset` does not. `SET_PUMP` is sent when the known state differs and the pump is not faulted. `RESET_PUMP` is sent only for `reset`. A command that already matches waits for the next state read, about `kPumpPollIntervalMs` (60 seconds), which publishes the same word again.

If no accepted on/off command arrives for `kPumpCommandTimeoutMs` (3 minutes), a pump that is on is turned off. A faulted pump stays faulted until `reset`.

Behaviour: [pump reference](pump-module.md).

## Publications

The controller publishes these. Subscribe to read them. QoS is 0.

| Topic | Retained | Payload | Who publishes |
| --- | --- | --- | --- |
| `{id}/slot/N` | yes | Slot snapshot. See below. | `ModuleSlotPublisher` |
| `{id}/slot/N/sensor/M` | yes for a reading; no for a failed immediate read | `connected <int32>`, `disconnected`, or `unavailable` | Sensor bridge |
| `{id}/slot/N/sensors` | yes | `<count> <connected indexes...>`, or `unavailable` | Sensor bridge |
| `{id}/slot/N/solenoid/M` | yes | `on`, `off`, `disconnected`, or `unavailable` | Solenoid bridge |
| `{id}/slot/N/solenoids` | yes | `<count> <connected indexes...>`, or `unavailable` | Solenoid bridge |
| `{id}/slot/N/pump` | yes | `on`, `off`, `fault`, or `unavailable` | Pump bridge |

`N` is 1..4. `M` is 1..16. Firmware slot 0 is topic 1.

`{id}/slot/N` is the public slot line, without a `Slot N:` prefix. The same words are on the USB snapshot with that prefix. The body is one of:

```text
Empty
Debouncing
Enumerating
Online Sensor addr=0x10
Online Solenoid addr=0x11
Online Pump addr=0x12
Unsupported type=0x02AA addr=0x13
Fault Nack
Fault BadCrc
Fault BadFrame
Fault Timeout
Fault Busy
```

`addr` is two uppercase hex digits. `type` is four uppercase hex digits. The name is `Sensor`, `Solenoid`, or `Pump`. An online type with no name is `Unknown`. An unchanged body is not sent again. A failed publish is retried. After a reconnect, a body the broker already accepted stays unpublished until the text changes. A new subscriber still receives it, because the broker kept the retained copy.

A reading or a solenoid or pump state is published again each time a new sample is stored, including when the text is the same. The next pass, with no new sample, publishes nothing for that point.

`unavailable` is retained once when that point disappears (unplug, a count that drops an index, or a slot that is no longer that type). A failed immediate sensor read is the exception: `unavailable` on `{id}/slot/N/sensor/M` is not retained.

A count query publishes nothing by itself. A sensor presence query publishes nothing on the reading topic. It can publish `{id}/slot/N/sensors` when the list becomes complete or the connected set changes.

Slot text and the retry rule: [module bus reference](module-bus.md).

## Formatting

- One path segment for `{id}`. The rules are the hostname rules: 1–31 characters, letters, digits, and hyphens, starting and ending with a letter or digit. A slash, space, `+`, or `#` is rejected at `set mqtt.prefix`.
- Command tokens are unsigned decimal. `01` is slot 1.
- Inventory text is the count, then each connected index, one space between numbers, no trailing space.
- Sensor values are the raw int32, so a negative value has a leading minus.
- Result words are the lowercase words in the tables above. `unavailable` is that word alone.

Changing the prefix publishes the current values once under the new root. Commands on the previous prefix are ignored after the change. Retained messages under the old prefix stay on the broker. The generation counter is in the [network reference](networking.md). The `set` line is in the [configuration reference](configuration.md).

## Errors

An ignored command publishes nothing and leaves the last retained result in place.

| Payload | What happens |
| --- | --- |
| Empty, a slot of 0, a slot above 4, or a trailing extra word | Ignored |
| `{id}/sensor/read` with one number, or a sensor above 16 | Ignored |
| `{id}/solenoids` with `ON`, `disconnected`, or no on/off word | Ignored |
| `{id}/solenoids` with 1..16 words, and that count is not the module count | Recorded, absence window restarted, states not applied |
| `{id}/solenoids` with more than 16 on/off words | Ignored |
| `{id}/pump` with `fault`, or any word other than `on`, `off`, or `reset` | Ignored |
| Fifth sensor read while four are still waiting | Dropped |
| Publish while MQTT is disconnected | Rejected. The publisher retries on a later pass |
| Immediate sensor read that fails, or a sensor number outside the reported count | Non-retained `unavailable` |
| Slot unplugged after a result was published | Retained `unavailable` once on that slot's topics |

While an ISP session is active, `Network` still delivers commands, and the handlers still record them. Slot status and sensor, solenoid, and pump results are published when those modules run again after the session. [Programming reference](programming.md).

## Configuration

| Item | Value |
| --- | --- |
| Default `{id}` | `kMqttDeviceId` = `watering` |
| Subscriptions | 5, all QoS 1 (`kSubscriptionCount`) |
| Inbound handlers | 4 accepted, 3 registered (`kMaxInboundHandlers`) |
| Outbound QoS | 0 |
| Broker port default | `1883`, from `main.cpp` |
| Client id default | `watering-controller`, separate from `{id}` |

A new command topic means raising `kSubscriptionCount`. One handler may serve more than one topic. A fourth handler still fits.

## Tests

- `test/test_desktop/test_mqtt_topics.cpp` — the five topic strings and the generation counter.
- `test/test_desktop/test_slot_publish.cpp` — retained slot text, retries, a new prefix.
- `test/test_desktop/test_sensor.cpp` — read, connected list, malformed payloads, `unavailable`.
- `test/test_desktop/test_solenoid.cpp` — desired state, inventory, the absence window.
- `test/test_desktop/test_pump.cpp` — on, off, reset, the absence window.
- `test/test_desktop/test_deep_modules.cpp` — the five subscribes through `Network`, and each module `update()`.

## Source

`lib/Logic/Network/MqttTopicLayout.cpp` builds the topic strings. `lib/Logic/Network/MqttService.cpp` subscribes and publishes. `lib/Drivers/PubSubClientAdapter.cpp` forwards publish with the retain flag and no QoS. The payload formatters are `_formatReading` and `_formatInventory` in `SensorMqttBridge.cpp`, `_formatState` and `_formatInventory` in `SolenoidMqttBridge.cpp`, `_formatState` in `PumpMqttBridge.cpp`, and `writeSlotStatusBody` in `SlotStatusText.cpp`.

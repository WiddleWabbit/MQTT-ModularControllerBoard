# MQTT reference

[Home](../home.md) · Guide: [Network](../networking.md)

Source: `lib/Logic/Network/MqttTopicLayout.h`, `MqttService.h`. Payloads are built in `ModuleSlotPublisher`, `ControllerInfoPublisher`, `SlotStatusText`, `SensorMqttBridge`, `SolenoidMqttBridge`, and `PumpMqttBridge`.

This page is the command catalog. It lists every subscription and every publication, the payload text, and a line you can publish to test. When a command is applied, the poll order, and the absence windows stay on the module reference. If a page and the firmware disagree, the firmware wins. Update this page and that module page together.

`{id}` is the topic root. It is `kMqttDeviceId` in `src/main.cpp` (`watering`) until `set mqtt.prefix` is applied. The examples below use `watering`. Replace that segment when the prefix is something else.

## Purpose

`MqttTopicLayout` builds six QoS 1 subscriptions. `MqttService` subscribes after the broker connection succeeds, and it stays connected only when every subscribe succeeds. Sensor, solenoid, and pump each register one handler. The sensor handler serves `{id}/sensor`, `{id}/sensor/read`, and `{id}/sensor/connected`. The solenoid handler serves both solenoid topics.

Outbound publishes are QoS 0. `IMqttClient::publish` has no QoS argument, and `PubSubClient`'s publish sets the retain bit only.

Publish a command with retain off. A retained command is delivered again each time the controller subscribes.

## Find a command

| You want                                   | Publish to                 | Payload                       | Result arrives on                              |
| ------------------------------------------ | -------------------------- | ----------------------------- | ---------------------------------------------- |
| Turn one sensor input on, off, or reset it | `{id}/sensor`              | `1 2 on`                      | `{id}/slot/1/sensor/2`                         |
| One sensor reading                         | `{id}/sensor/read`         | `1 2`                         | `{id}/slot/1/sensor/2`                         |
| Which sensor inputs are on                 | `{id}/sensor/connected`    | `1`                           | `{id}/slot/1/sensors`                          |
| Desired state of every solenoid output     | `{id}/solenoids`           | `1 on on off off`             | `{id}/slot/1/solenoid/1` and the other outputs |
| Which solenoid outputs are connected       | `{id}/solenoids/connected` | `1`                           | `{id}/slot/1/solenoids`                        |
| Pump on, off, or reset                     | `{id}/pump`                | `1 on`                        | `{id}/slot/1/pump`                             |
| Pump live sample, cycle, totals, or fault  | —                          | the controller publishes this | `{id}/slot/1/pump/now` and cycle, totals, fault |
| Whether a slot has a board, and which type | —                          | the controller publishes this | `{id}/slot/1` through `{id}/slot/4`            |
| Controller board and firmware revision     | —                          | the controller publishes this | `{id}/controller`                              |

Slot numbers in every payload are 1..4. Slot 1 is I2C `0x10`, slot 2 is `0x11`, slot 3 is `0x12`, slot 4 is `0x13`. Sensor and solenoid indexes in topics and in connected-list results are 1-based.

To learn a count before naming every output, publish the connected-list command and read the first number of the result.

Watch the whole tree, then publish one command:

```text
mosquitto_sub -h <broker> -t watering/# -v
mosquitto_pub -h <broker> -t watering/sensor/read -q 1 -m "1 2"
```

`<broker>` is `mqtt.host`. The port default is `1883`. `mosquitto_pub` leaves retain off unless `-r` is passed. `-q 1` matches the subscription. The controller does not read the inbound QoS.

A new subscriber receives the last retained slot lines and the controller identity from the broker. Sensor, solenoid, and pump results are not retained, so that subscriber does not receive a stale reading. The controller sends a result again when a new sample is stored, or when the text changes, or when the command asks for the list again. Why the slot lines and `{id}/controller` are retained is in [Publications](#publications).

## Subscriptions

All six are QoS 1. They are not stored in NVS. `set mqtt.prefix` rebuilds the list under the new root.

| Topic | Payload | Handler |
| --- | --- | --- |
| `{id}/solenoids` | `N on off ...` | Solenoid bridge |
| `{id}/solenoids/connected` | `N` | Solenoid bridge, same handler |
| `{id}/pump` | `N on`, `N off`, or `N reset` | Pump bridge |
| `{id}/sensor/read` | `N M` | Sensor bridge |
| `{id}/sensor/connected` | `N` | Sensor bridge, same handler |
| `{id}/sensor` | `N M on`, `N M off`, or `N M reset` | Sensor bridge, same handler |

Words are lowercase. Separators are spaces or tabs. Leading and trailing spaces and tabs are accepted. A `+`, a sign, a decimal point, or an empty payload is ignored. An ignored command publishes nothing.

`N` is the slot, 1..4. `M` is the sensor, 1..16. `0` and a slot above 4 are ignored. A sensor number above 16 is ignored.

### `{id}/sensor/read`

Asks for one reading. `1 2` is slot 1, sensor 2.

```text
mosquitto_pub -h <broker> -t watering/sensor/read -q 1 -m "1 2"
```

The result is published on `{id}/slot/N/sensor/M` with retain off. A read does not turn the input on. Off, fault, and disconnected publish that word and do not take a reading. On publishes the raw value:

```text
off
fault
disconnected
connected 2500
connected -4
```

`connected` is followed by one raw int32. There is no payload word `on`.

A read that cannot be done publishes `unavailable` on that same topic with retain off. The request waits while the slot is still coming online or the count is not known yet. Up to four reads can wait. A fifth, while four are waiting, is dropped.

Behaviour: [sensor reference](sensor-module.md).

### `{id}/sensor`

Turns one input on or off, or resets a fault. `1 2 on` is slot 1, sensor 2, desired on.

```text
mosquitto_pub -h <broker> -t watering/sensor -q 1 -m "1 2 on"
mosquitto_pub -h <broker> -t watering/sensor -q 1 -m "1 2 off"
mosquitto_pub -h <broker> -t watering/sensor -q 1 -m "1 2 reset"
```

The words are lowercase `on`, `off`, and `reset`. The result is published on `{id}/slot/N/sensor/M` with retain off, using the same words as a reading. After `on` or `off`, the controller reads the state on the next passes, and it reads the value when that state is on, without waiting for the 60 second cycle. `reset` is the only command that clears a fault. The input is left off. If it is still desired on, a later pass turns it on. There is no silence timer.

A fault state is followed by one `GET_FAULT`. The controller publishes the list on `{id}/slot/N/sensors/fault` with retain off. The text is the count, then one word per input. `3 none overcurrent none` is three inputs, and input 2 is overcurrent. Reason `0` is `none`. Reason `1` is `overcurrent`. Any other reason is a decimal, such as `1 9`. A fault that clears publishes `none` for that input. This topic is not a subscription. `kSubscriptionCount` stays 6.

`ON`, a missing word, an extra token, a slot outside 1..4, and a sensor outside 1..16 are ignored and publish nothing.

Behaviour: [sensor reference](sensor-module.md).

### `{id}/sensor/connected`

Asks for the connected-input list of one slot. The payload is the slot number alone.

```text
mosquitto_pub -h <broker> -t watering/sensor/connected -q 1 -m "1"
```

The result is published on `{id}/slot/N/sensors` with retain off. The text is the count, then each index whose state is on, 1-based, separated by a single space:

```text
2 1
0
4
4 1 2 4
```

`2 1` is two inputs, and input 1 is on. `0` is a count of 0. `4` is four inputs, none on. Off, fault, and disconnected are left out. An extra token is ignored. The list is sent when the count and every state are known. An unchanged list is sent again when this command asks.

Behaviour: [sensor reference](sensor-module.md).

### `{id}/solenoids`

Names the desired on or off state of every output on one slot. One lowercase word per reported output.

```text
mosquitto_pub -h <broker> -t watering/solenoids -q 1 -m "1 on on off off"
```

`1 on on off off` asks slot 1 for on, on, off, off. `SET_SOLENOID` is sent only where the known state differs. Each stored state is then published on `{id}/slot/N/solenoid/M` with retain off, as `on`, `off`, or `disconnected`.

The number of words must equal the count the module reported, or the states are not applied. A parsed command still restarts the absence window, including one whose length does not match. `ON`, a missing slot, and a slot outside 1..4 are ignored and do not restart the window.

`disconnected` is a result. It is not a word this command accepts.

If no accepted command arrives for `kSolenoidCommandTimeoutMs` (15 minutes), every output is turned off.

Behaviour: [solenoid reference](solenoid-module.md).

### `{id}/solenoids/connected`

Asks for the connected-output list of one slot. The payload is the slot number alone. It does not record a desired state, and it does not restart the absence window.

```text
mosquitto_pub -h <broker> -t watering/solenoids/connected -q 1 -m "1"
```

The result is published on `{id}/slot/N/solenoids` with retain off, in the same shape as the sensor list. An index is listed when that output is `on` or `off`. A `disconnected` output is omitted. `4 1 2 4` is four outputs, with 1, 2, and 4 connected. An unchanged list is sent again when this command asks.

Behaviour: [solenoid reference](solenoid-module.md).

### `{id}/pump`

Names the desired state, or asks for a reset. The payload is the slot, a space, and one word.

```text
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 on"
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 off"
mosquitto_pub -h <broker> -t watering/pump -q 1 -m "1 reset"
```

The result is published on `{id}/slot/N/pump` with retain off, as `on`, `off`, or `fault`. Held and running are both `on` there.

The controller also publishes, with retain off, and does not subscribe to:

```text
{id}/slot/N/pump/now      held 0 1 0 12000 4000 0
{id}/slot/N/pump/cycle    12000 0 4000 0 0 0 0 0
{id}/slot/N/pump/totals   0 0 12000 4000 0 0
{id}/slot/N/pump/fault    slow-start 0
```

`now` is the phase (`off`, `held`, `running`, or `fault`), then nPGD-low, enable-high, and cycled as `0` or `1`, then millivolts, milliamps, and open-cycle seconds. `cycle` is peak mV, minimum mV, peak mA, average mV, average mA, soft-start ms, power-good ms, and run seconds. `totals` is cycles, seconds, peak mV, peak mA, failed starts, and last run seconds. `fault` is `none`, `slow-start`, `overcurrent`, `undercurrent`, `stuck-on`, or a decimal reason, then the index. These four topics are not subscriptions. `kSubscriptionCount` stays 6.

`on` and `off` restart the absence window. `reset` does not. The info reads do not. Each accepted `on` sends `SET_PUMP` on, including when the pump is already on. While the absence window stays open, that on command is sent again every `kPumpPollIntervalMs` (60 seconds), and the reply publishes the same word again. Off is sent when the known state differs. `RESET_PUMP` is sent only for `reset`. `SET_PUMP` is not sent while the pump is faulted. `GET_FAULT` is sent once for that fault and the reason is published. Now, cycle, and totals repeat every `kPumpInfoIntervalMs` (15 seconds).

If no accepted on/off command arrives for `kPumpCommandTimeoutMs` (3 minutes), a pump that is on is turned off. A faulted pump stays faulted until `reset`.

Behaviour: [pump reference](pump-module.md).

## Publications

The controller publishes these. Subscribe to read them. QoS is 0.

`{id}/slot/N` and `{id}/controller` are retained. `ModuleSlotPublisher` sends a slot line when its text changes, not on a timer, so a subscriber who connects later still needs the broker to remember `Empty` or `Online Sensor addr=0x10 board 2 firmware 1`. `ControllerInfoPublisher` sends `board 1 firmware 8` once the broker accepts it. A rejected publish is retried. A reconnect does not send that identity again, because the broker kept the retained copy. Sensor readings, the connected-input list, the sensor fault list, solenoid states, the connected-output list, pump state, and the pump now, cycle, totals, and fault topics are published with retain off. Those results used to be retained. After the board was unplugged, or replaced, the broker kept the last payload and delivered it as if the point were still there.

A publish with retain off does not replace a message the broker already stored. This firmware does not publish an empty payload, and it does not delete those copies. Messages already on the broker, including ones under a previous device id, stay until they are removed there. A subscriber who is already connected receives each new sample, and `unavailable` when that point disappears. A subscriber who connects later receives the four slot lines, the controller identity, and any result the broker is still holding. The next sample arrives on the next poll, or when a command asks again.

| Topic | Retained | Payload | Who publishes |
| --- | --- | --- | --- |
| `{id}/slot/N` | yes | Slot snapshot. See below. | `ModuleSlotPublisher` |
| `{id}/controller` | yes | `board 1 firmware 8` | `ControllerInfoPublisher` |
| `{id}/slot/N/sensor/M` | no | `connected <int32>`, `disconnected`, or `unavailable` | Sensor bridge |
| `{id}/slot/N/sensors` | no | `<count> <connected indexes...>`, or `unavailable` | Sensor bridge |
| `{id}/slot/N/sensors/fault` | no | `<count> <reason...>`, or `unavailable` | Sensor bridge |
| `{id}/slot/N/solenoid/M` | no | `on`, `off`, `disconnected`, or `unavailable` | Solenoid bridge |
| `{id}/slot/N/solenoids` | no | `<count> <connected indexes...>`, or `unavailable` | Solenoid bridge |
| `{id}/slot/N/pump` | no | `on`, `off`, `fault`, or `unavailable` | Pump bridge |
| `{id}/slot/N/pump/now` | no | phase, three flags, mV, mA, run seconds | Pump bridge |
| `{id}/slot/N/pump/cycle` | no | eight numbers | Pump bridge |
| `{id}/slot/N/pump/totals` | no | six numbers | Pump bridge |
| `{id}/slot/N/pump/fault` | no | reason and index | Pump bridge |

`N` is 1..4. `M` is 1..16. Firmware slot 0 is topic 1.

`{id}/slot/N` is the public slot line, without a `Slot N:` prefix. The same words are on the USB snapshot with that prefix. The body is one of:

```text
Empty
Debouncing
Enumerating
Online Sensor addr=0x10 board 2 firmware 1
Online Solenoid addr=0x11 board 1 firmware 3
Online Pump addr=0x12 board 1 firmware 4
Unsupported type=0x02AA addr=0x13 board 1 firmware 2
Fault Nack
Fault BadCrc
Fault BadFrame
Fault Timeout
Fault Busy
```

`addr` is two uppercase hex digits. `type` is four uppercase hex digits. The name is `Sensor`, `Solenoid`, or `Pump`. An online type with no name is `Unknown`. Online and unsupported lines append `board N firmware N` from the identity word: the high byte is the board revision and the low byte is the firmware revision. Both numbers are unsigned decimals, 0..255, with no leading zeros. Empty, debouncing, enumerating, and fault lines omit that suffix. An unchanged body is not sent again. A failed publish is retried. After a reconnect, a body the broker already accepted stays unpublished until the text changes. A new subscriber still receives it, because the broker kept the retained copy.

`{id}/controller` is the controller itself, not a slot. `src/main.cpp` sets `kBoardRevision` to 1 and `kFirmwareRevision` to 8, so the payload is `board 1 firmware 8`. There is no subscription for it. `kSubscriptionCount` stays 6. A programming session skips `moduleBus.update()`, so this line waits with the slot lines.

A reading, a solenoid state, a pump state, or a pump info sample is published again each time a new sample is stored, including when the text is the same. The next pass, with no new sample, publishes nothing for that point. When a pump board leaves, `{id}/slot/N` becomes `Empty`. The now, cycle, totals, and fault topics are not published again until a new sample is stored.

`unavailable` is published with retain off when a sensor point, a solenoid point, or the pump state disappears (unplug, a count that drops an index, or a slot that is no longer that type). A failed immediate sensor read publishes the same non-retained `unavailable`. A subscriber who connects after either event does not receive that `unavailable`. The slot line, which is retained, shows `Empty` or the board that is there now.

A count query publishes nothing by itself. A sensor state query can publish `off`, `fault`, or `disconnected` on `{id}/slot/N/sensor/M`, and it can publish `{id}/slot/N/sensors` when the list becomes complete or the set of inputs that are on changes. On does not publish a reading until the value is stored. `GET_FAULT`, or a fault that clears, publishes `{id}/slot/N/sensors/fault`.

Slot text and the retry rule: [module bus reference](module-bus.md).

## Formatting

- One path segment for `{id}`. The rules are the hostname rules: 1–31 characters, letters, digits, and hyphens, starting and ending with a letter or digit. A slash, space, `+`, or `#` is rejected at `set mqtt.prefix`.
- Command tokens are unsigned decimal. `01` is slot 1.
- Inventory text is the count, then each connected index, one space between numbers, no trailing space.
- Sensor values are the raw int32, so a negative value has a leading minus.
- Result words are the lowercase words in the tables above. `unavailable` is that word alone.

Changing the prefix publishes the current values once under the new root. Commands on the previous prefix are ignored after the change. Retained messages under the old prefix stay on the broker. The generation counter is in the [network reference](networking.md). The `set` line is in the [configuration reference](configuration.md).

## Errors

An ignored command publishes no result. It does not change the retained slot line, and it does not publish a sensor, solenoid, or pump payload.

| Payload | What happens |
| --- | --- |
| Empty, a slot of 0, a slot above 4, or a trailing extra word | Ignored |
| `{id}/sensor/read` with one number, or a sensor above 16 | Ignored |
| `{id}/sensor` with `ON`, a missing word, or an extra token | Ignored |
| `{id}/solenoids` with `ON`, `disconnected`, or no on/off word | Ignored |
| `{id}/solenoids` with 1..16 words, and that count is not the module count | Recorded, absence window restarted, states not applied |
| `{id}/solenoids` with more than 16 on/off words | Ignored |
| `{id}/pump` with `fault`, or any word other than `on`, `off`, or `reset` | Ignored |
| Fifth sensor read while four are still waiting | Dropped |
| Publish while MQTT is disconnected | Rejected. The publisher retries on a later pass |
| Immediate sensor read that fails, or a sensor number outside the reported count | `unavailable` with retain off |
| Slot unplugged after a sensor, solenoid, or pump state was published | `unavailable` with retain off, once, on that topic. `{id}/slot/N` becomes `Empty` and stays retained. Pump now, cycle, totals, and fault stay as last published |

While an ISP session is active, `Network` still delivers commands, and the handlers still record them. Slot status and sensor, solenoid, and pump results are published when those modules run again after the session. [Programming reference](programming.md).

## Configuration

| Item | Value |
| --- | --- |
| Default `{id}` | `kMqttDeviceId` = `watering` |
| Subscriptions | 6, all QoS 1 (`kSubscriptionCount`) |
| Inbound handlers | 4 accepted, 3 registered (`kMaxInboundHandlers`) |
| Outbound QoS | 0 |
| Broker port default | `1883`, from `main.cpp` |
| Client id default | `watering-controller`, separate from `{id}` |

A new command topic means raising `kSubscriptionCount`. One handler may serve more than one topic. A fourth handler still fits.

## Tests

- `test/test_desktop/test_mqtt_topics.cpp` — the six topic strings and the generation counter.
- `test/test_desktop/test_slot_publish.cpp` — retained slot text, retries, a new prefix.
- `test/test_desktop/test_sensor.cpp`, `test_solenoid.cpp`, and `test_pump.cpp` — result payloads with retain off.
- `test/test_desktop/test_sensor.cpp` — on, off, reset, the state confirm, read, connected list, malformed payloads, `unavailable`.
- `test/test_desktop/test_solenoid.cpp` — desired state, inventory, the absence window.
- `test/test_desktop/test_pump.cpp` — on, off, reset, the absence window.
- `test/test_desktop/test_deep_modules.cpp` — the six subscribes through `Network`, and each module `update()`.

## Source

`lib/Logic/Network/MqttTopicLayout.cpp` builds the topic strings. `lib/Logic/Network/MqttService.cpp` subscribes and publishes. `lib/Drivers/PubSubClientAdapter.cpp` forwards publish with the retain flag and no QoS. The payload formatters are `_formatState` and `_formatInventory` in `SensorMqttBridge.cpp`, `_formatState` and `_formatInventory` in `SolenoidMqttBridge.cpp`, `_formatState` in `PumpMqttBridge.cpp`, and `writeSlotStatusBody` in `SlotStatusText.cpp`.

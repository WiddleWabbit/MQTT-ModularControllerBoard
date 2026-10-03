# Sensor reference

[Home](../home.md) · Guide: [Sensor module](../sensor-module.md)

Source: `lib/Logic/Sensor/SensorModule.h`, `SensorPoller.h`, `SensorMqttBridge.h`. Shared framing is in [the daughter contract](../daughter/contract.md).

## Purpose

Type `0x0200` reports up to 16 inputs. The host learns the count when the module is identified, then reads presence and a raw value for each input. Readings are published on every successful read, including when the value is unchanged.

The host sends these commands only while the slot is `Online`, the type is `0x0200`, and the protocol version is 1. `PING`, `GET_IDENTITY`, and `SET_ADDRESS` are the shared commands. `ECHO` is not part of this type.

## Classes

`SensorModule::update()` runs `SensorPoller`, then `SensorMqttBridge`. The public surface is that `update()`. The constructor registers one MQTT handler. That handler serves both `{id}/sensor/read` and `{id}/sensor/connected`.

The poller decides the query and stores the result. The bridge records the read and the connected-list request, and publishes. Neither one calls `I2cMaster`. The poller uses `ModuleHost::exchange`.

## Commands

Indexes on the wire are 0-based. Module numbers and sensor numbers in MQTT are 1-based. Module 1 is firmware slot 0, schematic slot 1, I2C address `0x10`. Sensor 1 is wire index 0.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| `0x41` | GET_SENSOR_COUNT | empty | `count` (`u8`, 0..16) |
| `0x42` | GET_SENSOR_CONNECTED | `index` (`u8`) | `index`, `connected` (`u8`, 0 or 1) |
| `0x43` | GET_SENSOR_READING | `index` (`u8`) | `index`, `connected`, `value` (`i32` big-endian) |

`value` is a raw module unit. This firmware does not scale it or attach a unit.

## Sequences

A first connection, and a module that is identified again, both start with `GET_SENSOR_COUNT`. Each successful identify is a new session. The previous count and samples are forgotten.

After the count is known, the first presence/reading cycle starts on the following poller pass. It does not wait out the poll interval. The cycle walks inputs from index 0 upward. Each input is `GET_SENSOR_CONNECTED`, then `GET_SENSOR_READING`. One command runs per pass, so two inputs take four passes. When the last reading finishes, the next cycle is due `kSensorPollIntervalMs` later.

A count of 0 stores that count and does not start a cycle.

Leaving `Online` drops the cache. That includes unplug and a health recovery that identifies the module again. A sense gap that leaves the slot `Online` keeps the cache. The count is queried again once the slot is an online sensor.

### More than one module

Scanning and the enumeration lock are in the [module bus reference](module-bus.md). Each sensor slot has its own count, samples, and poll timer. The poller still runs one query per pass, for one slot, in this order:

1. An immediate read whose slot is an online sensor and whose count is already known.
2. `GET_SENSOR_COUNT` for an online sensor that has not reported a count. Slot 1 is queried before slot 2. A queued immediate read for a module that has no count yet makes that module the next count query.
3. The next presence or reading step. A slot that has started a cycle finishes every input before another slot starts a cycle. When no cycle is underway, the lowest due slot starts. A slot is due when its count is known, the count is greater than 0, and its own poll interval has elapsed. The timer is clear after the count is stored, so the first cycle starts on the following pass. It is set when that slot's last reading finishes. Two modules therefore repeat on their own intervals, offset by the time the earlier cycle took.

A count query for a module that has just come online is step 2, so it runs before the next step of another module's cycle. That other cycle continues on the following pass.

Two sensors, one input each, after both have been identified:

```text
slot 1  GET_SENSOR_COUNT
slot 2  GET_SENSOR_COUNT
slot 1  GET_SENSOR_CONNECTED index 0
slot 1  GET_SENSOR_READING   index 0
        MQTT {id}/slot/1/sensor/1
slot 2  GET_SENSOR_CONNECTED index 0
slot 2  GET_SENSOR_READING   index 0
        MQTT {id}/slot/2/sensor/1
```

About one poll interval after slot 1's reading, slot 1 is due again. When slot 2 is still inside a cycle, slot 2 finishes first and slot 1 runs on the following passes.

Health pings stay inside `ModuleHost::update()`. The sensor query runs after that returns. The same loop can carry one enumeration or health transaction and one sensor query.

### Immediate read

```text
topic:   {id}/sensor/read
payload: 1 2
```

That asks for sensor 2 on module slot 1. The two numbers are decimal, separated by spaces or tabs, with optional whitespace around them. The slot must be 1..4 and the sensor must be 1..16. Anything else is ignored, including a single number, extra tokens, a different topic, or a number above 16. An ignored command publishes nothing.

The callback enqueues the request. Up to four requests can wait. A fifth, while four are still waiting, is dropped and publishes nothing. On a later poller pass the host sends `GET_SENSOR_READING`. The bridge publishes the result on `{id}/slot/N/sensor/M`.

If the named input is outside the module's reported count, the host does not send I2C. The bridge publishes non-retained `unavailable`. The same non-retained `unavailable` is published when the slot can never answer (empty, fault, unsupported, or a different type) or when three attempts come back busy or failed. That message leaves the previous retained reading on the broker.

While the module is still coming online, or its count is not known yet, the request stays queued. It is not failed and it is not published yet.

`{id}/sensor/read` with payload `2 1` names slot 2. Once that slot's count is known, the read is the next sensor query, ahead of either module's periodic step.

### Connected inputs

`{id}/sensor/connected` with payload `N` asks for the connected-input list of slot `N`. The handler records that request. It does not enqueue a reading. A missing slot, a slot outside 1..4, and extra tokens are ignored.

The list waits until the count is known and every input's presence is known. The text is the count, then each connected index, 1-based. Sensor 1 present and sensor 2 absent on a two-input board is `2 1`. A count of 0 is `0`. All absent is the count alone, such as `4`. An unchanged list is not sent again unless `{id}/sensor/connected` asked. A request that arrives before presence is complete waits, then publishes on the pass that completes it. A later reading that changes presence publishes the new text once.

## Publication

`{id}` is the device id from [Configuration](../configuration.md). It is `watering` until `set mqtt.prefix` is applied. A new id publishes the current readings once under the new topics. The generation counter that causes that is in the [network reference](networking.md).

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/N` | publish | Slot status, such as `Online Sensor addr=0x10` |
| `{id}/slot/N/sensor/M` | publish | One sensor input |
| `{id}/slot/N/sensors` | publish | count, then connected indexes, such as `2 1` |
| `{id}/sensor/read` | subscribe, QoS 1 | `N M` |
| `{id}/sensor/connected` | subscribe, QoS 1 | `N` |

`{id}/slot/N` is the slot snapshot from `ModuleSlotPublisher`. It is not a sensor reading.

Outbound publishes use the client default QoS. Publication waits until MQTT is connected. A rejected publish stays pending and is retried on a later bridge pass.

Successful readings use these retained payloads:

```text
connected 2500
connected -4
disconnected
```

`connected` is followed by the raw int32. `disconnected` is a reading whose connected flag is 0. The raw value is omitted. A presence result by itself is not published as a reading. The reading publish happens on the bridge pass after the reading has been stored, which is the same `loop()` pass as a successful read.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| Periodic `GET_SENSOR_READING` succeeds | `{id}/slot/N/sensor/M` | `connected <value>` or `disconnected` | yes |
| Immediate read succeeds | `{id}/slot/N/sensor/M` | `connected <value>` or `disconnected` | yes |
| Immediate read fails, or the sensor number is outside the reported count | `{id}/slot/N/sensor/M` | `unavailable` | no |
| Slot is no longer an online sensor, or the new count drops an input that had a reading | `{id}/slot/N/sensor/M` | `unavailable` | yes, once |
| Count and every presence are known, the connected set changes, or `{id}/sensor/connected` asks again | `{id}/slot/N/sensors` | `2 1` | yes |
| Slot is no longer an online sensor after a list was published | `{id}/slot/N/sensors` | `unavailable` | yes, once |
| Count query, a presence query that leaves an input unknown, malformed command, or a dropped extra command | — | nothing | — |
| Bridge pass with no new reading and no list change | — | nothing | — |

The list text is the count, then each connected index. It is compared as text. An unchanged list is not sent again unless `{id}/sensor/connected` asked. The list can be published on the pass that stores the last presence, before that input's reading.

Each stored reading is published, including a repeat of the same text. The next bridge pass, with no new reading, does not publish that input again. An immediate read publishes once for that read. The periodic snapshot does not emit a second copy on that pass.

A failed periodic step publishes nothing for that input. The last retained payload stays until a later reading succeeds, or until the input is gone. When a published input disappears, the bridge publishes retained `unavailable` once. After the module is identified again, the next successful reading replaces it.

`GET_SENSOR_COUNT` does not publish. `GET_SENSOR_CONNECTED` does not publish a reading. It can publish the connected list when that query completes the set or changes it.

### Example

Sensor in slot 1, two inputs, first connected with raw value 2500, second absent:

```text
I2C  GET_SENSOR_COUNT                         (no MQTT)
I2C  GET_SENSOR_CONNECTED  index 0            (no MQTT)
I2C  GET_SENSOR_READING    index 0
MQTT {id}/slot/1/sensor/1  retained  "connected 2500"
I2C  GET_SENSOR_CONNECTED  index 1
MQTT {id}/slot/1/sensors   retained  "2 1"
I2C  GET_SENSOR_READING    index 1
MQTT {id}/slot/1/sensor/2  retained  "disconnected"
```

About one poll interval after that second reading, the four queries run again. Both reading topics are published again, even when 2500 and `disconnected` are unchanged. The connected list is not sent again.

```text
MQTT {id}/sensor/read  payload "1 1"
I2C  GET_SENSOR_READING    index 0
MQTT {id}/slot/1/sensor/1  retained  "connected 2500"
```

The periodic cycle resumes on later passes.

## Errors

An index outside the reported count is `BadLength` from the module. The host treats a count above 16, a connected byte other than 0 or 1, or an echoed index that does not match the request as a failed query. `Busy` asks the host to repeat the same command. A busy sensor query does not, by itself, move the slot to `Fault`.

`Busy` or a failed query is tried up to three times for that step. The cycle then moves to the next step. Three failed count queries wait `kSensorPollIntervalMs` before the count is tried again.

## Configuration

`kSensorPollIntervalMs` in `src/main.cpp` is `60UL * 1000UL`. It is both the gap after a finished cycle and the wait after three failed count queries. A zero argument to `SensorPoller` selects the poller's own 60 second default. It does not disable the poll.

## Tests

`test/test_desktop/test_sensor.cpp` drives `FakeModuleDevice` and `FakeMqttClient`. It covers the three command frames, host rejection unless the slot is an online sensor, count-then-poll ordering, the poll-interval repeat, a module reset that reads the count again, a count of 0, busy retries, the immediate read, malformed commands, a missing sensor, publication of an unchanged periodic value, and retained `unavailable` after unplug. The connected-list tests cover the first publish, a repeat only when asked, a request that waits for presence, malformed payloads, a count of 0, all inputs absent, a presence change, unplug, and a new device id. The sensor tests use one sensor module. Two slots enumerating together are in `test_modules.cpp`. `test_deep_modules.cpp` drives `SensorModule::update()`, including `{id}/sensor/connected`.

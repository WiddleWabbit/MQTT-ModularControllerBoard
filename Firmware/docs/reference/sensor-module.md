# Sensor reference

[Home](../home.md) · Guide: [Sensor module](../sensor-module.md)

Source: `lib/Logic/Sensor/SensorModule.h`, `SensorPoller.h`, `SensorCommands.h`, `SensorMqttBridge.h`. Shared framing is in [the daughter contract](../daughter/contract.md). Command bytes are in `lib/Interfaces/ModuleProtocol.h`.

## Purpose

Type `0x0200` reports up to 16 inputs. The host learns the count when the module is identified, then reads each input's state. A raw value is read only while that state is on. Off, fault, and disconnected are published from the state reply. A fault is followed by one `GET_FAULT`. Readings, those states, and the fault list are published on every stored observation, including when the text is unchanged.

The host sends these commands only while the slot is `Online`, the type is `0x0200`, and the protocol version is 1. `PING`, `GET_IDENTITY`, `SET_ADDRESS`, and `GET_FAULT` are the shared commands. `ECHO` is not part of this type.

## Classes

`SensorModule::update()` runs `SensorPoller`, then `SensorMqttBridge`. The public surface is that `update()`. The constructor registers one MQTT handler. That handler serves `{id}/sensor`, `{id}/sensor/read`, and `{id}/sensor/connected`.

The poller decides the query and stores the result. The bridge records on, off, reset, the read, and the connected-list request, and publishes. Neither one calls `I2cMaster`. The poller uses `ModuleHost::exchange`.

## Commands

Indexes on the wire are 0-based. Module numbers and sensor numbers in MQTT are 1-based. Module 1 is firmware slot 0, schematic slot 1, I2C address `0x10`. Sensor 1 is wire index 0.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| `0x41` | GET_SENSOR_COUNT | empty | `count` (`u8`, 0..16) |
| `0x42` | GET_SENSOR_CONNECTED | `index` (`u8`) | `index`, `connected` (`u8`, 0 or 1) |
| `0x43` | GET_SENSOR_READING | `index` (`u8`) | `index`, `connected`, `value` (`i32` big-endian) |
| `0x44` | GET_SENSOR_STATE | `index` (`u8`) | `index`, `state` (`u8`) |
| `0x45` | SET_SENSOR | `index`, `off` or `on` | `index`, `state`. `Busy` when the loop has just closed |
| `0x46` | RESET_SENSOR | `index` (`u8`) | `index`, `state`. The module leaves the input off |
| `0x04` | GET_FAULT | empty | one record per input, in index order. Each record is `reason` (`u8`), then `index` (`u8`) |

`value` is a raw module unit. This firmware does not scale it or attach a unit.

`GET_FAULT` reason `0` is `none`. Reason `1` is `overcurrent`. Any other reason is published as a decimal. A count of 0 is an empty payload. A list that would pass `kMaxPayload` is `Unsupported` and stores nothing. The poller sends this only while some input is fault and that input's reason is not stored yet.

A state byte is off (`0`), on (`1`), fault (`2`), or disconnected (`3`). `SET_SENSOR` accepts only off or on. `RESET_SENSOR` is a separate command. The poller sends it only after `{id}/sensor` asks for `reset`.

`GET_SENSOR_CONNECTED` stays in the protocol. The poller does not send it. The connected list is the inputs whose state is on. A reading's connected flag is 1 only while the state is on, so the poller does not treat that flag as the state.

## States

| State | Meaning | MQTT on `{id}/slot/N/sensor/M` | In the connected list |
| --- | --- | --- | --- |
| Off | Loop open. This is how an input boots. | `off` | no |
| On | Loop closed and a sample is stored. | `connected <int32>` after the reading | yes |
| Fault | Latched. On and off do nothing. | `fault` | no |
| Disconnected | Loop closed, jack empty. | `disconnected` | no |

There is no payload word `on`. On is `connected` plus the raw value. Storing the on state does not publish until that reading is stored. Until then the previous payload stays, including across the `Busy` reply from `SET_SENSOR` on. That gap is not published as `unavailable`.

Desired on matches on and disconnected. Desired off matches off. Fault matches neither, and `SET_SENSOR` is not sent. A second on while the loop is already closed does not send `SET_SENSOR`. It still reads the state, and then the value, so the reading is published on the next passes.

## Sequences

One `update()` issues at most one exchange, in this order:

1. `GET_SENSOR_COUNT` when the count is unknown.
2. `GET_FAULT` when any input is fault and that input has no stored reason yet. One read covers the whole list. Three failures wait `kSensorPollIntervalMs`, then try again while a reason is still missing. That wait does not hold a reset, a set, or a confirm.
3. `RESET_SENSOR` when a reset is pending and the state is already known.
4. `SET_SENSOR` when the known state is not fault and does not match the desired on or off.
5. `GET_SENSOR_STATE` for an input that was just switched, or whose on or off command did not need a set.
6. `GET_SENSOR_READING` when that confirm reported on.
7. `GET_SENSOR_STATE` for a commanded input, or the head of the read queue, whose state is still unknown.
8. The head immediate read. A state other than on completes with that state and no reading. On sends `GET_SENSOR_READING`.
9. The periodic state cycle.

`SET_SENSOR` that returns `Busy` is accepted. The next pass is `GET_SENSOR_STATE`, not another on. Three busy confirms wait `kSensorPollIntervalMs` and stay pending, ahead of the periodic walk. Three failed sets drop that attempt and keep the desired state. The next periodic state that still differs tries again.

A reading whose connected flag is 0 does not publish `disconnected`. The state is read once more, and that confirm does not immediately read again.

A first connection, and a module that is identified again, both start with `GET_SENSOR_COUNT`. Each successful identify is a new session. The previous count and samples are forgotten. Desired on or off, and a pending reset, are kept. The new session does not send `RESET_SENSOR` just because the identity changed. After a reset reply of off, a later pass sends `SET_SENSOR` when the input is still desired on.

After the count is known, the first state cycle starts on the following poller pass. It does not wait out the poll interval. The cycle walks inputs from index 0 upward. Each input is `GET_SENSOR_STATE`. When the reply is on, the next pass is `GET_SENSOR_READING`, and the cycle then moves on. Off, fault, and disconnected publish from the state reply and are not read. When the last input finishes, the next cycle is due `kSensorPollIntervalMs` later.

A count of 0 stores that count and does not start a cycle. A command for an index outside the reported count is dropped once the count is known.

Leaving `Online` drops the cache. Empty, slot `Fault`, `Unsupported`, or a different type also drops the desired state and any pending reset, and publishes `unavailable` once. A sense gap that leaves the slot `Online` keeps the cache. While the slot is still enumerating, the desired state is kept and the cache is dropped.

### More than one module

Scanning and the enumeration lock are in the [module bus reference](module-bus.md). Each sensor slot has its own count, desired state, samples, fault reasons, and poll timer. The poller still runs one query per pass, for one slot. A missing count, and a due fault read, run before a reset, a set, the confirm that follows a switch, and another slot's periodic step. An in-progress state cycle finishes every input before another slot starts a cycle. When no cycle is underway, the lowest due slot starts. A slot is due when its count is known, the count is greater than 0, and its own poll interval has elapsed. The timer is clear after the count is stored, so the first cycle starts on the following pass. It is set when that slot's last input finishes.

Two sensors, one input each, both off, after both have been identified:

```text
slot 1  GET_SENSOR_COUNT
slot 2  GET_SENSOR_COUNT
slot 1  GET_SENSOR_STATE index 0
        MQTT {id}/slot/1/sensor/1  "off"
slot 2  GET_SENSOR_STATE index 0
        MQTT {id}/slot/2/sensor/1  "off"
```

About one poll interval after slot 1 finishes, slot 1 is due again. When slot 2 is still inside a cycle, slot 2 finishes first.

Health pings stay inside `ModuleHost::update()`. The sensor query runs after that returns. The same loop can carry one enumeration or health transaction and one sensor query.

### On, off, and reset

```text
topic:   {id}/sensor
payload: 1 2 on
```

That commands sensor 2 on module slot 1. The words are `on`, `off`, and `reset`, lowercase. The two numbers are decimal, separated by spaces or tabs, with optional whitespace around them. The slot must be 1..4 and the sensor must be 1..16. Anything else is ignored, including a missing word, `ON`, an extra token, or a different topic. An ignored command publishes nothing.

`on` and `off` store the desired state. `reset` stores a reset and does not change the desired state. The callback does not touch I2C.

After the state is known, the next poller pass sends `SET_SENSOR` when the state differs and is not fault. A missing fault reason is read first. The pass after the set is `GET_SENSOR_STATE`. When the confirm is on, the pass after that is `GET_SENSOR_READING`, and the bridge publishes `connected <value>`. When the confirm is disconnected, the bridge publishes `disconnected` and does not read. Off publishes `off`. None of these wait for the periodic cycle.

`reset` sends `RESET_SENSOR` once the state is known. A missing fault reason is read first. The reply is published. If the input is still desired on, a later pass sends `SET_SENSOR` on. A reset that is still faulted is published and not followed by a set. A periodic state that shows fault does not send `RESET_SENSOR`.

The pass after a state of fault sends `GET_FAULT` before a waiting reset, set, or confirm. The reply replaces the stored reason of every input. A later input that faults causes one more read. A reset, or a later state that leaves fault, stores `none` for that input and does not send `GET_FAULT`. The first off or on, with no fault yet, publishes nothing on the fault topic. A new identity forgets the list. Reason `0` while the state is still fault is stored as `none` and is not read again until that input leaves fault and faults again.

### Immediate read

```text
topic:   {id}/sensor/read
payload: 1 2
```

That asks for sensor 2 on module slot 1. It does not turn the input on. The slot must be 1..4 and the sensor must be 1..16. Anything else is ignored and publishes nothing.

The callback enqueues the request. Up to four requests can wait. A fifth, while four are still waiting, is dropped and publishes nothing. The state is read first when it is not known. A state other than on publishes that word and does not send `GET_SENSOR_READING`. On sends the reading, and the bridge publishes `connected <value>`.

If the named input is outside the module's reported count, the host does not send I2C. The bridge publishes `unavailable` with retain off. The same non-retained `unavailable` is published when the slot can never answer (empty, fault, unsupported, or a different type) or when three attempts come back busy or failed. A subscriber who is already connected sees that word. A subscriber who connects afterwards does not. The reason result topics are not retained is in the [MQTT reference](mqtt.md).

While the module is still coming online, or its count is not known yet, the request stays queued. It is not failed and it is not published yet.

### Connected inputs

`{id}/sensor/connected` with payload `N` asks for the connected-input list of slot `N`. The handler records that request. It does not enqueue a reading and it does not turn an input on. A missing slot, a slot outside 1..4, and extra tokens are ignored.

The list waits until the count is known and every input's state is known. The text is the count, then each index whose state is on, 1-based. Sensor 1 on and sensor 2 off on a two-input board is `2 1`. A count of 0 is `0`. None on is the count alone, such as `3` or `4`. Off, fault, and disconnected add nothing. An unchanged list is not sent again unless `{id}/sensor/connected` asked. A request that arrives before every state is known waits, then publishes on the pass that completes it. A later state that changes the set publishes the new text once.

An input whose state is on is listed before its reading has been stored.

## Publication

`{id}` is the device id from [Configuration](../configuration.md). It is `watering` until `set mqtt.prefix` is applied. A new id publishes the current readings once under the new topics. The generation counter that causes that is in the [network reference](networking.md). A copy-paste publish for each topic is in the [MQTT reference](mqtt.md).

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/N` | publish, retained, QoS 0 | Slot status from `ModuleSlotPublisher`, such as `Online Sensor addr=0x10` |
| `{id}/slot/N/sensor/M` | publish, QoS 0, retain off | `off`, `fault`, `disconnected`, `connected <int32>`, or `unavailable` |
| `{id}/slot/N/sensors` | publish, QoS 0, retain off | count, then indexes whose state is on, such as `2 1` |
| `{id}/slot/N/sensors/fault` | publish, QoS 0, retain off | count, then one reason per input, such as `3 none overcurrent none` |
| `{id}/sensor` | subscribe, QoS 1 | `N M on`, `N M off`, or `N M reset` |
| `{id}/sensor/read` | subscribe, QoS 1 | `N M` |
| `{id}/sensor/connected` | subscribe, QoS 1 | `N` |

`{id}/slot/N` is the slot snapshot from `ModuleSlotPublisher`. It is not a sensor reading.

Outbound publishes are QoS 0. Publication waits until MQTT is connected. A rejected publish stays pending and is retried on a later bridge pass.

Successful observations use these payloads. None of them are retained:

```text
off
fault
disconnected
connected 2500
connected -4
```

`connected` is followed by the raw int32. `disconnected` is a closed loop with an empty jack. It is not the word used for off or for fault.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| State is off, fault, or disconnected | `{id}/slot/N/sensor/M` | `off`, `fault`, or `disconnected` | no |
| Reading stored while on | `{id}/slot/N/sensor/M` | `connected <value>` | no |
| Immediate read of a state other than on | `{id}/slot/N/sensor/M` | that state's word | no |
| Immediate read fails, or the sensor number is outside the reported count | `{id}/slot/N/sensor/M` | `unavailable` | no |
| Slot is no longer an online sensor, or the new count drops an input that had a result | `{id}/slot/N/sensor/M` | `unavailable` | no, once |
| Count and every state are known, the on set changes, or `{id}/sensor/connected` asks again | `{id}/slot/N/sensors` | `2 1` | no |
| Slot is no longer an online sensor after a list was published | `{id}/slot/N/sensors` | `unavailable` | no, once |
| `GET_FAULT` succeeds, or a fault clears | `{id}/slot/N/sensors/fault` | `3 none overcurrent none`, `1 9`, or `3 none none none` | no |
| Slot is no longer an online sensor after a fault list was published | `{id}/slot/N/sensors/fault` | `unavailable` | no, once |
| Count query, on before its reading, malformed command, or a dropped extra command | — | nothing new | — |
| Bridge pass with no new observation and no list change | — | the result and the list are not repeated | — |

Readings and the connected list are not retained. A retained reading survived after the board was unplugged, and a new subscriber received it as if the input were still there. `{id}/slot/N` stays retained because that line is sent only when its text changes. The retain rule, including copies already stored on the broker, is in the [MQTT reference](mqtt.md).

The list text is the count, then each index whose state is on. It is compared as text. An unchanged list is not sent again unless `{id}/sensor/connected` asked. The list can be published on the pass that stores the last state, before that input's reading.

Each stored observation is published, including a repeat of the same text. The next bridge pass, with no new observation, does not publish that input again. An immediate read publishes once for that read.

`GET_SENSOR_COUNT` does not publish. A state of on does not publish a reading by itself. It can publish the connected list when that query completes the set or changes it.

### Example

Sensor in slot 1, two inputs, both off. Then sensor 1 is turned on and the jack has a sample:

```text
I2C  GET_SENSOR_COUNT
I2C  GET_SENSOR_STATE     index 0
MQTT {id}/slot/1/sensor/1  "off"
I2C  GET_SENSOR_STATE     index 1
MQTT {id}/slot/1/sensor/2  "off"
MQTT {id}/slot/1/sensors   "2"

MQTT {id}/sensor           payload "1 1 on"
I2C  SET_SENSOR            index 0, on     (Busy, previous "off" stays)
I2C  GET_SENSOR_STATE      index 0         (on, no reading yet)
I2C  GET_SENSOR_READING    index 0
MQTT {id}/slot/1/sensor/1  "connected 2500"
MQTT {id}/slot/1/sensors   "2 1"
```

About one poll interval after input 2's state, the state queries run again. An input that is still on is read on the following pass. Both result topics are published again when a new observation is stored. The connected list is not sent again.

```text
MQTT {id}/sensor/read  payload "1 1"
I2C  GET_SENSOR_READING    index 0
MQTT {id}/slot/1/sensor/1  "connected 2500"
```

The same read for an input that is off publishes `off` and does not send `GET_SENSOR_READING`.

## Errors

An index outside the reported count is `BadLength` from the module. The host treats a count above 16, a state byte other than 0..3, a connected byte other than 0 or 1, or an echoed index that does not match the request as a failed query. A fault payload whose length is not two bytes per known input, or whose index byte is not that record's position, is a failed query and stores nothing. `Unsupported` is the same. `Busy` asks the host to repeat the same command, except a `Busy` reply to `SET_SENSOR` on, which is accepted and followed by `GET_SENSOR_STATE`. A busy sensor query does not, by itself, move the slot to `Fault`.

`Busy` or a failed query is tried up to three times for that step. A failed state or reading step moves the cycle to the next input. Three failed count queries wait `kSensorPollIntervalMs` before the count is tried again. Three failed fault reads wait that same interval, then try the list again while a reason is still missing. Three failed sets or resets drop that attempt. The desired state is kept.

## Configuration

`kSensorPollIntervalMs` in `src/main.cpp` is `60UL * 1000UL`. It is the gap after a finished state cycle, the wait after three failed count queries, the backoff after three busy confirms, and the wait after three failed `GET_FAULT` reads. A zero argument to `SensorPoller` selects the poller's own 60 second default. It does not disable the poll.

There is no command-absence cutoff. Sensors are not turned off because `{id}/sensor` has been quiet, and a repeated on is not sent on a timer. The daughter has no command-hold timer. A second on while the loop is closed is a no-op on the wire.

## Tests

`test/test_desktop/test_sensor.cpp` drives `FakeModuleDevice` and `FakeMqttClient`.

- Command frames, a bad state byte, a short payload, and rejection unless the slot is an online sensor: `testHostReadsSensorCountConnectedAndReading`, `testHostRejectsSensorQueryUnlessOnlineSensor`, `testHostRejectsShortSensorPayload`.
- Count, then state, then a reading only when on, and the 60 second gap: `testPollerQueriesCountThenEachSensorAtInterval`. Three busy state replies skip that input: `testPollerRetriesBusyThenMovesOn`.
- Boot publishes `off` and does not read: `testBootPublishesOffAndDoesNotRead`.
- `1 1 on` sends `SET_SENSOR`, then `GET_SENSOR_STATE`, then `GET_SENSOR_READING`, and publishes `connected` without waiting out the interval: `testSensorOnConfirmsStateThenPublishesReading`. An open jack publishes `disconnected` and does not read: `testSensorOnWithOpenJackPublishesDisconnected`. A second off, or on while already on, confirms state and does not send `SET_SENSOR`: `testSensorOffConfirmsAndSecondOffSkipsSet`, `testSensorOnWhileAlreadyOnConfirmsWithoutSet`.
- Fault ignores on and off. Only `reset` sends `RESET_SENSOR`, and a desired on is applied after the input is off: `testSensorFaultIgnoresOnAndOffUntilReset`. A periodic fault is not reset. The next pass reads `GET_FAULT` once: `testPeriodicFaultIsNotReset`.
- The fault list is one record per input. Input 2 overcurrent publishes `3 none overcurrent none`, and a later fault reads the list once more: `testHostReadsSensorFaultList`, `testSensorFaultListPublishesReasons`. On and off do not send `GET_FAULT`: `testSensorOnAndOffDoNotReadFault`. Reset publishes `none` with no second read: `testSensorFaultClearPublishesNoneWithoutRead`. Three busy replies wait out the poll interval: `testSensorFaultReadWaitsAfterThreeBusyReplies`. The fault read runs before a confirm and before a reset: `testSensorFaultReadRunsBeforeConfirm`, `testSensorFaultReadRunsBeforeReset`. An odd payload and `Unsupported` store nothing: `testSensorFaultBadListIsNotStored`, `testSensorFaultUnsupportedIsNotStored`. Reason `9` publishes `1 9`, and unplug publishes `unavailable`: `testSensorFaultDecimalReasonAndUnplug`.
- An immediate read of an off input publishes `off` and does not read: `testImmediateReadOfOffPublishesOff`. A fifth queued read is dropped: `testFifthSensorReadIsDropped`.
- A new identity keeps desired on and does not reset. A slot that comes back as another type drops it: `testSensorCommandKeepsDesiredAcrossNewIdentity`, `testSensorCommandDropsDesiredWhenSlotChangesType`.
- The connected list names only inputs whose state is on: `testConnectedListNamesOnlyInputsThatAreOn` and the earlier list cases. Unplug publishes `unavailable` with retain off.

The sensor tests use one sensor module. Two slots enumerating together are in `test_modules.cpp`. `test_deep_modules.cpp` drives `SensorModule::update()`, including the six subscriptions and `{id}/sensor/connected`.

# Sensor module

Type `0x0200` is the first daughter-module type. One module occupies one of
the four motherboard slots and reports up to 16 sensor inputs. The controller
learns that count when the module is identified, then reads presence and a raw
value for each input. Readings are published over MQTT on every successful
read, including when the value is unchanged.

Framing, CRC, addressing, and the commands every module must answer are in
[MODULES.md](MODULES.md). This page is the sensor-specific behaviour.

## Roles

`ModuleHost` is the only I2C caller. `SensorPoller` decides which sensor
query to run. `SensorMqttBridge` turns the read command into a poller request
and publishes readings. `loop()` calls them in this order, after MQTT has
been serviced:

```text
moduleHost.update()
sensorPoller.update()
sensorMqttBridge.update()
```

The MQTT callback only enqueues a request. It does not touch I2C. The poller
issues at most one sensor transaction per pass, and that transaction is
separate from the single transaction inside `moduleHost.update()`.

Serial status prints the slot line (`Online Sensor addr=0x10`). It does not
print individual inputs.

## I2C commands

The host sends these only while the slot is `Online`, the identified type is
`0x0200`, and the protocol version is 1. Any other type or protocol version
never receives them. Indexes on the wire are 0-based.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| 0x41 | GET_SENSOR_COUNT | empty | `count` (`u8`, 0..16) |
| 0x42 | GET_SENSOR_CONNECTED | `index` (`u8`) | `index`, `connected` (`u8`, 0 or 1) |
| 0x43 | GET_SENSOR_READING | `index` (`u8`) | `index`, `connected`, `value` (`i32` big-endian) |

`value` is a raw module unit. This firmware does not scale it or attach a
unit. An index outside the module's reported count is `BadLength` from the
module. The host treats a count above 16, a connected byte other than 0 or 1,
or an echoed index that does not match the request as a failed query. `Busy`
asks the host to repeat the same command. A busy sensor query does not, by
itself, move the slot to `Fault`.

`PING`, `GET_IDENTITY`, and `SET_ADDRESS` are the shared module commands from
[MODULES.md](MODULES.md). `ECHO` is not part of this type.

## When the host reads

A module restart that is identified again, and a first connection, both start
with `GET_SENSOR_COUNT`. The poller treats each successful identify as a new
session and forgets the previous count and samples.

After the count is known, the first presence/reading cycle starts on the
following poller pass. It does not wait out the minute first. The cycle walks
inputs from index 0 upward. Each input is `GET_SENSOR_CONNECTED`, then
`GET_SENSOR_READING`. One command runs per pass, so a module with two inputs
takes four passes for a full cycle. When the last reading of the cycle
finishes, the next cycle is due 60 seconds later.

A count of 0 stores that count and does not start a presence/reading cycle.

`Busy` or a failed query is tried up to three times for that step. The cycle
then moves to the next step. Three failed count queries wait 60 seconds
before the count is tried again.

An immediate read, described below, runs before the count query and before
the periodic cycle. While the module is still enumerating, or the count is
not known yet, the immediate read stays queued and the count query proceeds.

Leaving `Online` drops the cached count and samples. That includes unplug and
a health recovery that identifies the module again. A sense glitch that
leaves the slot `Online` keeps the cache. The slot is queried for its count
again once it is an online Sensor module.

## MQTT topics

Module numbers and sensor numbers in MQTT are 1-based. Module 1 is firmware
slot 0, schematic slot 1, I2C address `0x10`. Sensor 1 is wire index 0.

| Topic | Direction | Payload |
| --- | --- | --- |
| `watering/slot/N` | publish | Slot status, such as `Online Sensor addr=0x10` |
| `watering/slot/N/sensor/M` | publish | One sensor input |
| `watering/sensor/read` | subscribe, QoS 1 | `N M` |

`watering/slot/N` is the slot snapshot from `ModuleSlotPublisher`. It changes
when the slot text changes (`Empty`, `Enumerating`, `Online Sensor addr=0x10`,
`Fault Nack`, and the other slot states). It is not a sensor reading.

The firmware subscribes to `watering/sensor/read` together with
`watering/solenoids` and `watering/pump`. Outbound sensor publishes use the
client default QoS. Publication waits until MQTT is connected. A rejected
publish stays pending and is retried on a later bridge pass.

## MQTT command

The only sensor command is an immediate reading:

```text
topic:   watering/sensor/read
payload: 1 2
```

That asks for sensor 2 on module slot 1. The two numbers are decimal, separated
by spaces or tabs, with optional whitespace around them. The slot must be
1..4 and the sensor must be 1..16. Anything else is ignored, including a
single number, extra tokens, a different topic, or a number above 16. An
ignored command publishes nothing.

The callback enqueues the request. Up to four requests can wait. A fifth
request, while four are still waiting, is dropped and publishes nothing. On
a later poller pass the host sends `GET_SENSOR_READING` for that input. The
bridge then publishes the result on `watering/slot/N/sensor/M`.

If the named input is outside the module's reported count, the host does not
send I2C. The bridge publishes non-retained `unavailable` on that sensor
topic. The same non-retained `unavailable` is published when the slot can
never answer (empty, fault, unsupported, or a different module type) or when
three attempts come back busy or failed. That message leaves the previous
retained reading on the broker.

While the module is still coming online, or its count is not known yet, the
request stays queued. It is not failed and it is not published yet.

## What is published, and when

Successful readings use these retained payloads:

```text
connected 2500
connected -4
disconnected
```

`connected` is followed by the raw int32 from `GET_SENSOR_READING`.
`disconnected` is the reading whose connected flag is 0. The raw value is
omitted in that case. A presence result by itself is not published. The
publish happens on the bridge pass after the reading has been stored, which
is the same `loop()` pass as a successful read.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| Periodic `GET_SENSOR_READING` succeeds | `watering/slot/N/sensor/M` | `connected <value>` or `disconnected` | yes |
| Immediate read succeeds | `watering/slot/N/sensor/M` | `connected <value>` or `disconnected` | yes |
| Immediate read fails, or the sensor number is outside the reported count | `watering/slot/N/sensor/M` | `unavailable` | no |
| Slot is no longer an online Sensor module, or the new count no longer includes an input that had a reading | `watering/slot/N/sensor/M` | `unavailable` | yes, once |
| Count query, presence query, malformed command, or a dropped extra command | — | nothing | — |
| Bridge pass with no new reading | — | nothing | — |

Each stored reading is published, including a repeat of the same text. The
next bridge pass, with no new reading, does not publish that input again.
An immediate read publishes once for that read. The periodic snapshot does
not emit a second copy of the same read on that pass.

A failed periodic step publishes nothing for that input. The last retained
payload stays until a later reading succeeds, or until the input is gone.

When a published input disappears, the bridge publishes retained
`unavailable` once. A new subscriber then sees that the input is gone.
Typical causes are unplug, a module restart that clears the session, and a
count that no longer includes that sensor. After the module is identified
again, the next successful reading replaces `unavailable` with
`connected <value>` or `disconnected`.

`GET_SENSOR_COUNT` and `GET_SENSOR_CONNECTED` never publish on their own.
The count only decides which sensor topics exist. Presence is folded into
the following reading, whose response carries the connected flag.

## Example

Sensor module in slot 1, two inputs, first input connected with raw value
2500, second input absent:

```text
I2C  GET_SENSOR_COUNT                         (no MQTT)
I2C  GET_SENSOR_CONNECTED  index 0            (no MQTT)
I2C  GET_SENSOR_READING    index 0
MQTT watering/slot/1/sensor/1  retained  "connected 2500"
I2C  GET_SENSOR_CONNECTED  index 1            (no MQTT)
I2C  GET_SENSOR_READING    index 1
MQTT watering/slot/1/sensor/2  retained  "disconnected"
```

About 60 seconds after that second reading, the four I2C queries run again.
Both sensor topics are published again, even when 2500 and `disconnected`
are unchanged.

```text
MQTT watering/sensor/read  payload "1 1"
I2C  GET_SENSOR_READING    index 0
MQTT watering/slot/1/sensor/1  retained  "connected 2500"
```

That immediate publish happens on the loop that performs the read. The
periodic cycle resumes on later passes.

## Testing

Desktop tests in `test/test_desktop/test_sensor.cpp` drive `FakeModuleDevice`
and `FakeMqttClient`. They cover the three command frames, host rejection
unless the slot is an online Sensor module, count-then-poll ordering, the
60 second repeat, a module reset that reads the count again, a count of 0,
busy retries, the immediate read command, malformed commands, a missing
sensor, publication of an unchanged periodic value, and retained
`unavailable` after unplug.

```text
pio test -e native
```

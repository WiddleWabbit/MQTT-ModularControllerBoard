# Solenoid module

Type `0x0100` is the solenoid daughter module. One module occupies one of the
four motherboard slots and drives up to 16 solenoid outputs. The controller
learns that count when the module is identified, reads each output about once
a minute, and publishes the state. An MQTT command names the desired on/off
state of every output. The controller sends an on or off command only for
outputs that are not already in that state.

Framing, CRC, addressing, and the commands every module must answer are in
[MODULES.md](MODULES.md). This page is the solenoid-specific behaviour.

## Roles

`ModuleHost` is the only I2C caller. `SolenoidPoller` decides which solenoid
query to run. `SolenoidMqttBridge` turns `watering/solenoids` into a desired
state and publishes each output. `loop()` calls them after the sensor poller:

```text
moduleHost.update()
sensorPoller.update()
solenoidPoller.update()
sensorMqttBridge.update()
solenoidMqttBridge.update()
```

The MQTT callback only records the desired state. It does not touch I2C.
The poller issues at most one solenoid transaction per pass. That transaction
is separate from the host health ping and from the sensor query, so one
`loop()` can carry one of each.

Serial status prints the slot line (`Online Solenoid addr=0x10`). It does
not print individual outputs.

## I2C commands

The host sends these only while the slot is `Online`, the identified type is
`0x0100`, and the protocol version is 1. Any other type or protocol version
never receives them. Indexes on the wire are 0-based.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| 0x50 | GET_SOLENOID_COUNT | empty | `count` (`u8`, 0..16) |
| 0x51 | GET_SOLENOID_STATE | `index` (`u8`) | `index`, `state` (`u8`) |
| 0x52 | SET_SOLENOID | `index`, `desired` (`u8`, 0 or 1) | `index`, `state` (`u8`) |

`state` and `desired` use these values:

| Value | Name | Meaning |
| --- | --- | --- |
| 0 | off | Output is off |
| 1 | on | Output is on |
| 2 | disconnected | Nothing is connected to that output |

`SET_SOLENOID` accepts only off or on. A disconnected output stays
disconnected and reports state 2. The host does not send `SET_SOLENOID` to
an output it already knows is disconnected.

An index outside the module's reported count is `BadLength` from the module.
The host treats a count above 16, a state byte other than 0, 1, or 2, or an
echoed index that does not match the request as a failed query. `Busy` asks
the host to repeat the same command. A busy solenoid query does not, by
itself, move the slot to `Fault`.

`PING`, `GET_IDENTITY`, and `SET_ADDRESS` are the shared module commands from
[MODULES.md](MODULES.md). `ECHO` is not part of this type.

## When the host reads

A module restart that is identified again, and a first connection, both start
with `GET_SOLENOID_COUNT`. The poller treats each successful identify as a new
session and forgets the previous count and states. A desired-state command
already accepted for that motherboard slot is applied again after the new
count is known, when the new count matches the command.

After the count is known, the first state cycle starts on the following
poller pass. It does not wait out the minute first. The cycle reads outputs
from index 0 upward, one `GET_SOLENOID_STATE` per pass. When the last output
of the cycle finishes, the next cycle is due 60 seconds later.

A count of 0 stores that count and does not start a state cycle.

`Busy` or a failed query is tried up to three times for that step. The cycle
then moves to the next output. Three failed count queries wait 60 seconds
before the count is tried again.

Leaving `Online` drops the cached count and states. That includes unplug and
a health recovery that identifies the module again. A sense glitch that
leaves the slot `Online` keeps the cache.

## MQTT command

Desired states arrive on the existing subscription:

```text
topic:   watering/solenoids
payload: 1 on on off off
```

That asks module slot 1 to make solenoid 1 on, solenoid 2 on, solenoid 3 off,
and solenoid 4 off. The first number is the motherboard slot, 1..4. Module 1
is firmware slot 0, schematic slot 1, I2C address `0x10`. Each following word
is `on` or `off`, lowercase, for solenoid 1, 2, and so on. Words are separated
by spaces or tabs. The number of words must equal the count the module
reported. Anything else is ignored, including a missing slot number, `ON`,
a slot outside 1..4, or a different topic.

The callback records the desired state and does not touch I2C. On later
poller passes the host compares that desired state with the state it last
read. It sends `SET_SOLENOID` only where they differ:

```text
solenoid 1 off, desired on  -> SET on
solenoid 2 off, desired on  -> SET on
solenoid 3 off, desired off -> no command
solenoid 4 off, desired off -> no command
```

One `SET_SOLENOID` runs per pass, lowest index first. An output that is
disconnected is left alone. A desired state whose length does not match the
module count is not applied.

If the command arrives before the count or the states are known, the poller
reads the count, then reads each unknown state, then sends only the `SET`
commands that still differ. A command for a slot that is still enumerating
waits. A command for an empty, faulted, unsupported, or different module is
dropped.

A newer command for the same slot replaces the previous desired state. The
last accepted command is applied again after that module is identified again,
when the new count is the same length.

## Command absence

`watering/solenoids` is expected about once a minute. If no accepted command
arrives for 15 minutes, the controller turns every solenoid output off.

That 15 minute window is `kSolenoidCommandTimeoutMs` in `src/main.cpp`
(`15UL * 60UL * 1000UL`). It is passed into `SolenoidPoller` with the one
minute poll interval, `kSolenoidPollIntervalMs`. Changing the constant in
`main.cpp` changes how long the controller waits. Zero is not a way to
disable the cutoff: a zero argument selects these same defaults.

The window starts when the poller first runs. Each accepted command restarts
it, including a command whose length later does not match the module. A
malformed payload does not restart it. When the window elapses, every online
solenoid module is given desired state off. Outputs that are already off or
disconnected are not sent a command. Outputs that are on receive
`SET_SOLENOID` off, one per pass. A module that comes online after the window
has already elapsed is turned off once its count is known. The next accepted
command restarts the window and applies that command's desired states.

A failed on or off is tried up to three times for that pass. A later state
poll that still shows an output on, while the absence cutoff is in effect,
tries off again.

## MQTT topics

Module numbers and solenoid numbers in MQTT are 1-based. Solenoid 1 is wire
index 0.

| Topic | Direction | Payload |
| --- | --- | --- |
| `watering/slot/N` | publish | Slot status, such as `Online Solenoid addr=0x10` |
| `watering/slot/N/solenoid/M` | publish | One output: `on`, `off`, or `disconnected` |
| `watering/solenoids` | subscribe, QoS 1 | `N on off ...` |

`watering/slot/N` is the slot snapshot from `ModuleSlotPublisher`. It is not
a solenoid state.

Outbound state publishes use the client default QoS. Publication waits until
MQTT is connected. A rejected publish stays pending and is retried on a later
bridge pass.

## What is published, and when

Each stored state is published, including a repeat of the same text. The
publish happens on the bridge pass after the state has been stored. The next
bridge pass, with no new state, does not publish that output again.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| Periodic `GET_SOLENOID_STATE` succeeds | `watering/slot/N/solenoid/M` | `on`, `off`, or `disconnected` | yes |
| `SET_SOLENOID` returns a state | `watering/slot/N/solenoid/M` | `on`, `off`, or `disconnected` | yes |
| Slot is no longer an online Solenoid module, or the new count no longer includes an output that had a state | `watering/slot/N/solenoid/M` | `unavailable` | yes, once |
| Count query, malformed command, or a command that is still waiting for the count | — | nothing | — |
| Bridge pass with no new state | — | nothing | — |

A failed periodic step publishes nothing for that output. The last retained
payload stays until a later read or set succeeds, or until the output is gone.

## More than one module

Scanning and the enumeration lock are described in [MODULES.md](MODULES.md).
Each Solenoid slot has its own count, states, and minute timer. The poller
still runs one solenoid query per pass. A desired-state command for a slot
whose count is not known yet is the next count query, before another slot's
periodic read. Applying a command then takes the bus ahead of the periodic
reads. Unplugging one module publishes `unavailable` on that module's
solenoid topics only.

The absence cutoff is one window for the whole controller. Any accepted
command restarts it. When it elapses, every solenoid module is turned off.

## Example

Solenoid module in slot 1, four outputs, all off. The minute read publishes
the four states. The command `1 on on off off` then turns on the first two
and leaves the other two alone:

```text
I2C  GET_SOLENOID_COUNT
I2C  GET_SOLENOID_STATE index 0
MQTT watering/slot/1/solenoid/1  retained  "off"
I2C  GET_SOLENOID_STATE index 1
MQTT watering/slot/1/solenoid/2  retained  "off"
I2C  GET_SOLENOID_STATE index 2
MQTT watering/slot/1/solenoid/3  retained  "off"
I2C  GET_SOLENOID_STATE index 3
MQTT watering/slot/1/solenoid/4  retained  "off"

MQTT watering/solenoids  payload "1 on on off off"
I2C  SET_SOLENOID index 0 on
MQTT watering/slot/1/solenoid/1  retained  "on"
I2C  SET_SOLENOID index 1 on
MQTT watering/slot/1/solenoid/2  retained  "on"
```

About 60 seconds after the last state read, the four reads run again and
publish again, even when the text is unchanged. If no accepted
`watering/solenoids` command arrives for `kSolenoidCommandTimeoutMs`
(15 minutes in `src/main.cpp`), outputs that are on are commanded off.

## Testing

Desktop tests in `test/test_desktop/test_solenoid.cpp` drive `FakeModuleDevice`
and `FakeMqttClient`. They cover the three command frames, host rejection
unless the slot is an online Solenoid module, count-then-poll ordering, the
60 second repeat, a module reset that reads the count again, a count of 0,
busy retries, a desired state that sets only the outputs that differ,
disconnected outputs left uncommanded, the 15 minute default, a configured
absence cutoff, and retained `unavailable` after unplug. The tests use one
Solenoid module.

```text
pio test -e native
```

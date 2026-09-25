# Pump module

Type `0x0300` is the pump daughter module. One module occupies one of the
four motherboard slots and drives one pump. The controller reads that pump
when the module is identified, reads it again about once a minute, and
publishes the state. An MQTT command names the desired on or off state. The
controller sends an on or off command only when the pump is not already in
that state. A fault is cleared only by a separate reset command.

Framing, CRC, addressing, and the commands every module must answer are in
[MODULES.md](MODULES.md). This page is the pump-specific behaviour.

## Roles

`ModuleHost` is the only I2C caller. `PumpPoller` decides which pump query
to run. `PumpMqttBridge` turns `watering/pump` into a desired state or a
reset, and publishes the pump state. `loop()` calls them after the solenoid
poller:

```text
moduleHost.update()
sensorPoller.update()
solenoidPoller.update()
pumpPoller.update()
sensorMqttBridge.update()
solenoidMqttBridge.update()
pumpMqttBridge.update()
```

The MQTT callback only records the request. It does not touch I2C. The
poller issues at most one pump transaction per pass. That transaction is
separate from the host health ping and from the sensor and solenoid queries,
so one `loop()` can carry one of each.

Serial status prints the slot line (`Online Pump addr=0x10`). It does not
print the pump state. That state is the MQTT payload below.

## I2C commands

The host sends these only while the slot is `Online`, the identified type is
`0x0300`, and the protocol version is 1. Any other type or protocol version
never receives them. There is one pump on the module, so the commands carry
no index.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| 0x60 | GET_PUMP_STATE | empty | `state` (`u8`) |
| 0x61 | SET_PUMP | `desired` (`u8`, 0 or 1) | `state` (`u8`) |
| 0x62 | RESET_PUMP | empty | `state` (`u8`) |

`state` and `desired` use these values:

| Value | Name | Meaning |
| --- | --- | --- |
| 0 | off | Pump is off |
| 1 | on | Pump is on |
| 2 | fault | Pump is faulted |

`SET_PUMP` accepts only off or on. A faulted pump stays faulted until
`RESET_PUMP`. The host does not send `SET_PUMP` while the last known state
is fault, and it sends `RESET_PUMP` only after an MQTT reset.

`Busy` asks the host to repeat the same command. A busy pump query does not,
by itself, move the slot to `Fault`. The host treats a state byte other than
0, 1, or 2 as a failed query.

`PING`, `GET_IDENTITY`, and `SET_ADDRESS` are the shared module commands from
[MODULES.md](MODULES.md). `ECHO` is not part of this type.

## When the host reads

A module restart that is identified again, and a first connection, both start
with `GET_PUMP_STATE`. The poller treats each successful identify as a new
session and forgets the previous state. A desired on/off command already
accepted for that motherboard slot is applied again after the new state is
known, when that state is off or on and differs from the command.

The first read does not wait out the minute. The next read is due 60 seconds
after the previous successful read.

`Busy` or a failed query is tried up to three times for that step. Three
failed state reads wait 60 seconds before the state is tried again.

Leaving `Online` drops the cached state. That includes unplug and a health
recovery that identifies the module again. A sense glitch that leaves the
slot `Online` keeps the cache.

## MQTT command

Commands arrive on the existing subscription:

```text
topic:   watering/pump
payload: 1 on
```

`1 on` and `1 off` name the desired state. `1 reset` resets the pump. The
number is the motherboard slot, 1..4. Module 1 is firmware slot 0, schematic
slot 1, I2C address `0x10`. The word is lowercase. Words are separated by
spaces or tabs. Anything else is ignored, including a missing slot number,
`ON`, a slot outside 1..4, an extra word, or a different topic.

The callback records the request and does not touch I2C. On later poller
passes the host compares a desired on/off state with the state it last read.
It sends `SET_PUMP` only when they differ:

```text
pump off, desired on   -> SET on
pump on,  desired on   -> no command
pump on,  desired off  -> SET off
pump fault, desired on -> no SET; wait for reset
```

`on` and `off` restart the command-absence window. `reset` does not. A
malformed payload does not restart it either.

If the command arrives before the state is known, the poller reads the state,
then sends `SET_PUMP` only when it still differs. A command for a slot that
is still enumerating waits. A command for an empty, faulted, unsupported, or
different module is dropped.

A newer on/off command for the same slot replaces the previous desired state.
The last accepted on/off command is applied again after that module is
identified again.

## Reset

`1 reset` sends `RESET_PUMP` and nothing else until that command finishes.
The host does not reset a pump because the state is fault, and it does not
reset one because the on/off commands have stopped.

When the reset returns off or on, a later pass sends `SET_PUMP` if the
desired state still differs. When the desired state is on, that is how the
pump is turned on again after the fault is cleared. When the reset still
returns fault, the host publishes `fault` and does not send `SET_PUMP`.

## Command absence

`watering/pump` on/off commands are expected about once a minute. If no
accepted on/off command arrives for 3 minutes, the controller turns off
every pump that is on.

That 3 minute window is `kPumpCommandTimeoutMs` in `src/main.cpp`
(`3UL * 60UL * 1000UL`). It is passed into `PumpPoller` with the one minute
poll interval, `kPumpPollIntervalMs`. Changing the constant in `main.cpp`
changes how long the controller waits. Zero is not a way to disable the
cutoff: a zero argument selects these same defaults.

The window starts when the poller first runs. Each accepted `on` or `off`
restarts it. A reset does not. A malformed payload does not. When the window
elapses, every online pump module is given desired state off. A pump that is
already off is not sent a command. A pump that is on receives `SET_PUMP` off.
A pump that is faulted is left faulted. A module that comes online after the
window has already elapsed is turned off once its state is known and that
state is on. The next accepted on/off command restarts the window and applies
that command.

The absence cutoff is one window for the whole controller. Any accepted
on/off command, for any slot, restarts it. When it elapses, every pump that
is on is turned off.

A failed on, off, or reset is tried up to three times. A later state poll
that still shows the pump on, while the absence cutoff is in effect, tries
off again.

## MQTT topics

Module numbers in MQTT are 1-based.

| Topic | Direction | Payload |
| --- | --- | --- |
| `watering/slot/N` | publish | Slot status, such as `Online Pump addr=0x10` |
| `watering/slot/N/pump` | publish | `on`, `off`, or `fault` |
| `watering/pump` | subscribe, QoS 1 | `N on`, `N off`, or `N reset` |

`watering/slot/N` is the slot snapshot from `ModuleSlotPublisher`. It is not
the pump state.

Outbound state publishes use the client default QoS. Publication waits until
MQTT is connected. A rejected publish stays pending and is retried on a later
bridge pass.

## What is published, and when

Each stored state is published, including a repeat of the same text. The
publish happens on the bridge pass after the state has been stored. The next
bridge pass, with no new state, does not publish that pump again.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| `GET_PUMP_STATE` succeeds | `watering/slot/N/pump` | `on`, `off`, or `fault` | yes |
| `SET_PUMP` or `RESET_PUMP` returns a state | `watering/slot/N/pump` | `on`, `off`, or `fault` | yes |
| Slot is no longer an online Pump module | `watering/slot/N/pump` | `unavailable` | yes, once |
| State query, malformed command, or a command that is still waiting for the state | — | nothing | — |
| Bridge pass with no new state | — | nothing | — |

A failed read publishes nothing. The last retained payload stays until a
later read, set, or reset succeeds, or until the pump is gone.

## More than one module

Scanning and the enumeration lock are described in [MODULES.md](MODULES.md).
Each Pump slot has its own state and minute timer. The poller still runs one
pump query per pass. A command for a slot whose state is not known yet is
the next state read, before another slot's periodic read. Applying a command
or a reset then takes the bus ahead of the periodic reads. Unplugging one
module publishes `unavailable` on that module's pump topic only.

## Example

Pump module in slot 1, off. The first read publishes `off`. The command
`1 on` turns it on. A later fault is published and left alone until
`1 reset`, after which the still-desired on state turns it on again:

```text
I2C  GET_PUMP_STATE
MQTT watering/slot/1/pump  retained  "off"

MQTT watering/pump  payload "1 on"
I2C  SET_PUMP on
MQTT watering/slot/1/pump  retained  "on"

I2C  GET_PUMP_STATE
MQTT watering/slot/1/pump  retained  "fault"

MQTT watering/pump  payload "1 reset"
I2C  RESET_PUMP
MQTT watering/slot/1/pump  retained  "off"
I2C  SET_PUMP on
MQTT watering/slot/1/pump  retained  "on"
```

About 60 seconds after the last state read, the read runs again and publishes
again, even when the text is unchanged. If no accepted `watering/pump` on/off
command arrives for `kPumpCommandTimeoutMs` (3 minutes in `src/main.cpp`), a
pump that is on is commanded off.

## Testing

Desktop tests in `test/test_desktop/test_pump.cpp` drive `FakeModuleDevice`
and `FakeMqttClient`. They cover the three command frames, host rejection
unless the slot is an online Pump module, the state read and its 60 second
repeat, a module reset that reads the state again, busy retries, a desired
state that sets the pump only when it differs, fault published and left
alone, reset followed by on when that is still desired, the 3 minute default,
a configured absence cutoff, reset not extending that cutoff, and retained
`unavailable` after unplug. The tests use one Pump module, plus one command
addressed to slot 2.

```text
pio test -e native
```

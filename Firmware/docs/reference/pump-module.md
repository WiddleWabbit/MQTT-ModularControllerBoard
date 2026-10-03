# Pump reference

[Home](../home.md) · Guide: [Pump module](../pump-module.md)

Source: `lib/Logic/Pump/PumpModule.h`, `PumpPoller.h`, `PumpMqttBridge.h`. Shared framing is in [the daughter contract](../daughter/contract.md).

## Purpose

Type `0x0300` drives one pump. The host reads it when the module is identified, reads it again on the poll interval, and publishes `on`, `off`, or `fault`. `{id}/pump` names the desired state or asks for a reset. `SET_PUMP` is sent only when the known state differs, and it is not sent while the pump is faulted. `RESET_PUMP` is sent only after an MQTT reset.

The host sends these commands only while the slot is `Online`, the type is `0x0300`, and the protocol version is 1. There is no index. `ECHO` is not part of this type.

## Classes

`PumpModule::update()` runs `PumpPoller`, then `PumpMqttBridge`. The constructor registers the `{id}/pump` handler. The poller decides the query. The bridge records on, off, or reset, and publishes the stored state.

## Commands

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| `0x60` | GET_PUMP_STATE | empty | `state` (`u8`) |
| `0x61` | SET_PUMP | `desired` (`u8`, 0 or 1) | `state` (`u8`) |
| `0x62` | RESET_PUMP | empty | `state` (`u8`) |

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | off | Pump is off |
| 1 | on | Pump is on |
| 2 | fault | Pump is faulted |

`SET_PUMP` accepts only off or on. A faulted pump stays faulted until `RESET_PUMP`.

## Sequences

A first connection, and a module that is identified again, both start with `GET_PUMP_STATE`. The previous state is forgotten. A desired on/off command already accepted for that slot is applied again after the new state is known, when that state is off or on and differs from the command.

The first read does not wait out the poll interval. The next read is due `kPumpPollIntervalMs` after the previous successful read.

Leaving `Online` drops the cached state. A sense gap that leaves the slot `Online` keeps it.

`PumpPoller::update()` picks one query in this order:

1. `GET_PUMP_STATE` for an online pump whose state is not known. A command for that slot is the next state read, ahead of another slot's periodic read.
2. `RESET_PUMP` when a reset is pending and the state is known.
3. `SET_PUMP` when the desired on/off state differs from the known state, and the state is not fault.
4. The periodic `GET_PUMP_STATE`.

```text
topic:   {id}/pump
payload: 1 on
```

`1 on` and `1 off` name the desired state. `1 reset` resets the pump. The number is the slot, 1..4. Module 1 is firmware slot 0, address `0x10`. The word is lowercase. Anything else is ignored, including a missing slot number, `ON`, a slot outside 1..4, an extra word, or a different topic.

The callback records the request and does not touch I2C. `on` and `off` restart the absence window. `reset` does not. A malformed payload does not. A command for a slot that is still enumerating waits. A command for an empty, faulted slot, an unsupported type, or a different module is dropped.

```text
pump off, desired on   -> SET on
pump on,  desired on   -> no command
pump on,  desired off  -> SET off
pump fault, desired on -> no SET; wait for reset
```

A newer on/off command for the same slot replaces the previous desired state. The last accepted on/off command is applied again after the module is identified again.

### Reset

`1 reset` sends `RESET_PUMP` and nothing else until that command finishes. The host does not reset a pump because the state is fault, and it does not reset one because the on/off commands have stopped.

When the reset returns off or on, a later pass sends `SET_PUMP` if the desired state still differs. When the desired state is on, that turns the pump on again after the fault clears. When the reset still returns fault, the host publishes `fault` and does not send `SET_PUMP`.

### Command absence

`{id}/pump` on/off commands are expected about once a minute. If no accepted on/off command arrives for 3 minutes, the controller turns off every pump that is on.

The window is `kPumpCommandTimeoutMs` in `src/main.cpp` (`3UL * 60UL * 1000UL`). It starts when the poller first runs. Each accepted `on` or `off` restarts it. A reset does not. When the window elapses, every online pump is given desired state off. A pump that is already off is not commanded. A pump that is on receives `SET_PUMP` off. A pump that is faulted is left faulted. A module that comes online after the window has elapsed is turned off once its state is known and that state is on.

The window is one timer for the whole controller. Any accepted on/off command, for any slot, restarts it.

### More than one module

Each pump slot has its own state and poll timer. The poller still runs one query per pass. Unplugging one module publishes `unavailable` on that module's pump topic only.

## Publication

`{id}` is the device id from [Configuration](../configuration.md). A new id publishes the current pump state once. See the [network reference](networking.md).

Module numbers in MQTT are 1-based.

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/N` | publish | Slot status, such as `Online Pump addr=0x10` |
| `{id}/slot/N/pump` | publish | `on`, `off`, or `fault` |
| `{id}/pump` | subscribe, QoS 1 | `N on`, `N off`, or `N reset` |

`{id}/slot/N` is the slot snapshot. It is not the pump state. Outbound publishes use the client default QoS. A rejected publish stays pending.

Each stored state is published, including a repeat of the same text, on the bridge pass after it was stored. The next pass does not publish that pump again.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| `GET_PUMP_STATE` succeeds | `{id}/slot/N/pump` | `on`, `off`, or `fault` | yes |
| `SET_PUMP` or `RESET_PUMP` returns a state | `{id}/slot/N/pump` | `on`, `off`, or `fault` | yes |
| Slot is no longer an online pump | `{id}/slot/N/pump` | `unavailable` | yes, once |
| State query, malformed command, or a command still waiting for the state | — | nothing | — |
| Bridge pass with no new state | — | nothing | — |

A failed read publishes nothing. The last retained payload stays until a later read, set, or reset succeeds, or until the pump is gone.

### Example

Slot 1, off. Then `1 on`. A later fault stays until `1 reset`, after which the still-desired on state turns it on again:

```text
I2C  GET_PUMP_STATE
MQTT {id}/slot/1/pump  retained  "off"

MQTT {id}/pump  payload "1 on"
I2C  SET_PUMP on
MQTT {id}/slot/1/pump  retained  "on"

I2C  GET_PUMP_STATE
MQTT {id}/slot/1/pump  retained  "fault"

MQTT {id}/pump  payload "1 reset"
I2C  RESET_PUMP
MQTT {id}/slot/1/pump  retained  "off"
I2C  SET_PUMP on
MQTT {id}/slot/1/pump  retained  "on"
```

About one poll interval after the last state read, the read runs again and publishes again, even when the text is unchanged. If no accepted on/off command arrives for `kPumpCommandTimeoutMs`, a pump that is on is commanded off.

## Errors

`Busy` repeats the same command. A busy pump query does not, by itself, move the slot to `Fault`. A state byte other than 0, 1, or 2 is a failed query.

`Busy` or a failed query is tried up to three times. Three failed state reads wait `kPumpPollIntervalMs`. A failed on, off, or reset is tried up to three times. A later state poll that still shows the pump on, while the absence cutoff is in effect, tries off again.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kPumpPollIntervalMs` | 60 seconds | Gap after a successful state read, and the wait after three failed reads |
| `kPumpCommandTimeoutMs` | 3 minutes | Absence window that turns a pump that is on off |

A zero argument selects the poller's own default for that argument. It does not disable the poll or the cutoff. A reset does not refresh the absence window.

## Tests

`test/test_desktop/test_pump.cpp` covers the three command frames, host rejection unless the slot is an online pump, the state read and its 60 second repeat, a module reset that reads the state again, busy retries, a desired state that sets the pump only when it differs, fault published and left alone, reset followed by on when that is still desired, the 3 minute default, a configured absence cutoff, reset not extending that cutoff, and retained `unavailable` after unplug. The tests use one pump module, plus one command addressed to slot 2. `test_deep_modules.cpp` drives `PumpModule::update()`.

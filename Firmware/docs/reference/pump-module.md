# Pump reference

[Home](../home.md) · Guide: [Pump module](../pump-module.md)

Source: `lib/Logic/Pump/PumpModule.h`, `PumpPoller.h`, `PumpMqttBridge.h`. Shared framing is in [the daughter contract](../daughter/contract.md).

## Purpose

Type `0x0300` drives one pump. The host reads the state when the module is identified, and publishes `on`, `off`, or `fault`. Held and running are both `on` on that topic. The now topic names the phase. `{id}/pump` names the desired state or asks for a reset. Each accepted `on` sends `SET_PUMP` on, including when the pump is already on, and that on is repeated at the poll interval while the absence window is open. The repeat's reply is the state sample, so a held pump is not also read with `GET_PUMP_STATE` on that pass. Off is sent when the known state differs. `SET_PUMP` is not sent while the pump is faulted. `RESET_PUMP` is sent only after an MQTT reset.

Once the state is known, `GET_PUMP_NOW`, `GET_PUMP_CYCLE`, and `GET_PUMP_TOTALS` run on `kPumpInfoIntervalMs`. `GET_FAULT` runs once after a state result of fault. The pump reply is one record. It is not on the info period. The sensor poller sends the same command for its own list.

The host sends pump commands only while the slot is `Online`, the type is `0x0300`, and the protocol version is 1. There is no pump index on the state commands. `ECHO` is not part of this type.

## Classes

`PumpModule::update()` runs `PumpPoller`, then `PumpMqttBridge`. The constructor registers the `{id}/pump` handler. The poller decides the query. The bridge records on, off, or reset, and publishes the stored state and info samples.

## Commands

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| `0x60` | GET_PUMP_STATE | empty | `state` (`u8`) |
| `0x61` | SET_PUMP | `desired` (`u8`, 0 or 1) | `state` (`u8`) |
| `0x62` | RESET_PUMP | empty | `state` (`u8`) |
| `0x63` | GET_PUMP_NOW | empty | 8 bytes, below |
| `0x64` | GET_PUMP_CYCLE | empty | 16 bytes, eight `u16` |
| `0x65` | GET_PUMP_TOTALS | empty | 16 bytes, below |
| `0x04` | GET_FAULT | empty | `reason` (`u8`), `index` (`u8`) |

The pump poller sends `GET_FAULT` only to type `0x0300`, and only while the state is fault. The reply is one record, a list of length 1. The sensor poller sends the same command for one record per input. A daughter type with no fault read does not have to answer it.

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | off | Pump is off |
| 1 | on | Pump is on. Held and running both report this. |
| 2 | fault | Pump is faulted |

`SET_PUMP` accepts only off or on. A faulted pump stays faulted until `RESET_PUMP`. Payloads are big-endian. A 16-byte payload uses length field 18. These reads do not refresh the board's on-command age.

`GET_PUMP_NOW` is phase (`u8`), flags (`u8`), millivolts (`u16`), milliamps (`u16`), open-cycle run seconds (`u16`).

| Phase | Word |
| ---: | --- |
| 0 | `off` |
| 1 | `held` |
| 2 | `running` |
| 3 | `fault` |

A phase outside 0..3 fails the read. Flag bits are `0x01` nPGD low, `0x02` enable high, and `0x04` cycled since the last entry to off. Other flag bits are not printed. Each published flag is `0` or `1`.

`GET_PUMP_CYCLE` is eight `u16` values: peak mV, minimum mV after power-good (0 until then), peak mA, average mV, average mA, soft-start ms, power-good ms, run seconds.

`GET_PUMP_TOTALS` is completed cycles (`u32`), conducting seconds (`u32`), lifetime peak mV (`u16`), lifetime peak mA (`u16`), failed starts (`u16`), last completed run seconds (`u16`).

| Reason | Word |
| ---: | --- |
| 0 | `none` |
| 1 | `slow-start` |
| 2 | `overcurrent` |
| 3 | `undercurrent` |
| 4 | `stuck-on` |

Any other reason is published as that decimal. The index is always published. On this pump the index is 0.

## Sequences

A first connection, and a module that is identified again, both start with `GET_PUMP_STATE`. The previous state is forgotten. A desired on/off command already accepted for that slot is applied again after the new state is known, when that state is off or on and differs from the command.

The first read does not wait out the poll interval. The next read is due `kPumpPollIntervalMs` after the previous successful read.

Leaving `Online` drops the cached state. A sense gap that leaves the slot `Online` keeps it.

`PumpPoller::update()` picks one query in this order:

1. `GET_PUMP_STATE` for an online pump whose state is not known. A command for that slot is the next state read, ahead of another slot's periodic read.
2. `RESET_PUMP` when a reset is pending and the state is known.
3. `SET_PUMP` when the desired on/off state differs from the known state, and the state is not fault.
4. `SET_PUMP` on when the desired state is on, the known state is on, and a refresh is due. A refresh is due on the pass after each accepted `on`, and again `kPumpPollIntervalMs` after the previous successful on. That reply postpones the state read to the same time.
5. `GET_FAULT` when the known state is fault and this episode has no stored reason yet. One read per episode. Three failures wait `kPumpInfoIntervalMs`, then try again while the state is still fault and the reason is still unknown.
6. One due info read: `GET_PUMP_NOW`, else `GET_PUMP_CYCLE`, else `GET_PUMP_TOTALS`. The first of each is due on the next free pass after the state is known. Each is due again `kPumpInfoIntervalMs` after its own success. When all three are due they still go one per pass. An earlier slot is served before a later one, and that slot's now, then cycle, then totals, finish before the next slot. Three failures skip that one record until its next interval. The others continue. These reads do not restart the absence window and do not change on, off, or reset.
7. The periodic `GET_PUMP_STATE`.

A due reset, a differing `SET_PUMP`, or a due on refresh runs before a due info read. At a shared deadline the due info reads still run before the periodic state read. A fault skips `SET_PUMP` on that pass and leaves the on command pending, so `GET_FAULT` can run then.

```text
topic:   {id}/pump
payload: 1 on
```

`1 on` and `1 off` name the desired state. `1 reset` resets the pump. The number is the slot, 1..4. Module 1 is firmware slot 0, address `0x10`. The word is lowercase. Anything else is ignored, including a missing slot number, `ON`, a slot outside 1..4, an extra word, or a different topic.

The callback records the request and does not touch I2C. `on` and `off` restart the absence window. `reset` does not. A malformed payload does not. A command for a slot that is still enumerating waits. A command for an empty, faulted slot, an unsupported type, or a different module is dropped.

```text
pump off, desired on            -> SET on
pump on,  on command just in    -> SET on
pump on,  poll interval elapsed -> SET on
pump on,  neither of those due  -> no command
pump on,  desired off           -> SET off
pump fault, desired on          -> no SET; wait for reset
```

A newer on/off command for the same slot replaces the previous desired state. The last accepted on/off command is applied again after the module is identified again.

### Reset

`1 reset` sends `RESET_PUMP` and nothing else until that command finishes. The host does not reset a pump because the state is fault, and it does not reset one because the on/off commands have stopped.

When the reset returns off or on, a later pass sends `SET_PUMP` if the desired state still differs. When the desired state is on, that turns the pump on again after the fault clears. When the reset still returns fault, the host publishes `fault` and does not send `SET_PUMP`. If that fault's reason was already stored, it is not read again. If it was not, the next pass sends `GET_FAULT`.

A state that leaves fault stores reason 0 and index 0 and publishes `none 0`. That pass does not send `GET_FAULT`. The first off or on, when the pump was not faulted, does not publish `none`. A new identity forgets the reason. If the new state is fault, it is read once more. Reason 0 while the state is still fault is published as `none 0` and counts as that episode's reason.

### Command absence

`{id}/pump` on/off commands are expected about once a minute. While the window is open and the desired state is on, the controller also sends `SET_PUMP` on at that same pace, so a pump board can turn itself off when those commands stop. If no accepted on/off command arrives for 3 minutes, the controller turns off every pump that is on.

The window is `kPumpCommandTimeoutMs` in `src/main.cpp` (`3UL * 60UL * 1000UL`). It starts when the poller first runs. Each accepted `on` or `off` restarts it. A reset does not. A now, cycle, or totals read does not. When the window elapses, every online pump is given desired state off. A pump that is already off is not commanded. A pump that is on receives `SET_PUMP` off. A pump that is faulted is left faulted. A module that comes online after the window has elapsed is turned off once its state is known and that state is on.

The window is one timer for the whole controller. Any accepted on/off command, for any slot, restarts it.

### More than one module

Each pump slot has its own state and poll timer. The poller still runs one query per pass. Unplugging one module publishes `unavailable` on `{id}/slot/N/pump` only. The slot line is updated by the bus. Now, cycle, totals, and the fault reason are left as last published.

## Publication

`{id}` is the device id from [Configuration](../configuration.md). A new id publishes the current pump state once. See the [network reference](networking.md). A copy-paste publish for each topic is in the [MQTT reference](mqtt.md).

Module numbers in MQTT are 1-based.

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/N` | publish, retained, QoS 0 | Slot status from `ModuleSlotPublisher`, such as `Online Pump addr=0x10` |
| `{id}/slot/N/pump` | publish, QoS 0, retain off | `on`, `off`, `fault`, or `unavailable` |
| `{id}/slot/N/pump/now` | publish, QoS 0, retain off | phase, three flags, millivolts, milliamps, run seconds |
| `{id}/slot/N/pump/cycle` | publish, QoS 0, retain off | eight numbers, below |
| `{id}/slot/N/pump/totals` | publish, QoS 0, retain off | six numbers, below |
| `{id}/slot/N/pump/fault` | publish, QoS 0, retain off | reason word or decimal, then the index |
| `{id}/pump` | subscribe, QoS 1 | `N on`, `N off`, or `N reset` |

The info topics are publishes. `kSubscriptionCount` stays 6. Nothing new is subscribed.

`{id}/slot/N` is the slot snapshot. It is not the pump state. Outbound publishes are QoS 0. A rejected publish stays pending.

Each stored state is published, including a repeat of the same text, on the bridge pass after it was stored. The next pass does not publish that pump again.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| `GET_PUMP_STATE` succeeds | `{id}/slot/N/pump` | `on`, `off`, or `fault` | no |
| `SET_PUMP` or `RESET_PUMP` returns a state | `{id}/slot/N/pump` | `on`, `off`, or `fault` | no |
| `GET_PUMP_NOW` succeeds | `{id}/slot/N/pump/now` | `held 0 1 0 12000 4000 0` is phase, nPGD-low, enable-high, cycled, mV, mA, run seconds | no |
| `GET_PUMP_CYCLE` succeeds | `{id}/slot/N/pump/cycle` | `12000 0 4000 0 0 0 0 0` | no |
| `GET_PUMP_TOTALS` succeeds | `{id}/slot/N/pump/totals` | `0 0 12000 4000 0 0` is cycles, seconds, peak mV, peak mA, failed starts, last run seconds | no |
| `GET_FAULT` succeeds, or a fault clears | `{id}/slot/N/pump/fault` | `none 0`, `slow-start 0`, `overcurrent 0`, `undercurrent 0`, `stuck-on 0`, or `9 0` | no |
| Slot is no longer an online pump | `{id}/slot/N/pump` | `unavailable` | no, once |
| Slot is no longer an online pump | `{id}/slot/N/pump/now`, `/cycle`, `/totals`, `/fault` | the last sample stays; nothing new is published | — |
| State query, malformed command, or a command still waiting for the state | — | nothing | — |
| Bridge pass with no new sample | — | that sample is not repeated | — |
| A new identity while the slot is still an online pump | the info topics | the next samples | no |

While a record is unknown, the bridge forgets the generation it last published, so a new identity whose counters start again at 1 is published. `{id}/slot/N` is the retained notice that the board has left.

Pump state is not retained. A retained `on` or `fault` survived after the board was unplugged, and a new subscriber received it as if the pump were still there. `{id}/slot/N` stays retained because that line is sent only when its text changes. The retain rule, including copies already stored on the broker, is in the [MQTT reference](mqtt.md).

A failed read publishes nothing. The next successful read, set, or reset is published with retain off.

### Example

Slot 1, off. Then `1 on`. A later fault stays until `1 reset`, after which the still-desired on state turns it on again:

```text
I2C  GET_PUMP_STATE
MQTT {id}/slot/1/pump  "off"

MQTT {id}/pump  payload "1 on"
I2C  SET_PUMP on
MQTT {id}/slot/1/pump  "on"

I2C  SET_PUMP on
MQTT {id}/slot/1/pump  "fault"

MQTT {id}/pump  payload "1 reset"
I2C  RESET_PUMP
MQTT {id}/slot/1/pump  "off"
I2C  SET_PUMP on
MQTT {id}/slot/1/pump  "on"
```

About one poll interval after the last successful on, `SET_PUMP` on runs again and publishes again, even when the text is unchanged. A pump that is off or faulted is read on that interval instead. If no accepted on/off command arrives for `kPumpCommandTimeoutMs`, a pump that is on is commanded off, and the on refresh stops.

## Errors

`Busy` repeats the same command. A busy pump query does not, by itself, move the slot to `Fault`. A state byte other than 0, 1, or 2 is a failed query. A now payload shorter than 8 bytes, or a phase outside 0..3, is a failed query. A short cycle, totals, or fault payload is a failed query.

`Busy` or a failed query is tried up to three times. Three failed state reads wait `kPumpPollIntervalMs`. A failed on, off, or reset is tried up to three times. Three failed on refreshes wait `kPumpPollIntervalMs` and then try again while the desired state stays on. Three failed now, cycle, totals, or fault reads wait `kPumpInfoIntervalMs` and then try that one record again. The other records are not held back. A later state poll that still shows the pump on, while the absence cutoff is in effect, tries off again.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kPumpPollIntervalMs` | 60 seconds | Gap after a successful state read, between on refreshes, and the wait after three failed state reads or three failed on refreshes |
| `kPumpCommandTimeoutMs` | 3 minutes | Absence window that turns a pump that is on off |
| `kPumpInfoIntervalMs` | 15 seconds | Gap after each successful now, cycle, or totals read, and the wait after three failed reads of that record or of `GET_FAULT` |

A zero argument selects the poller's own default for that argument, including 15 seconds for the info interval. It does not disable the poll, the info reads, or the cutoff. A reset does not refresh the absence window.

## Tests

`test/test_desktop/test_pump.cpp` covers the command frames, host rejection unless the slot is an online pump, a short payload and a phase of 4, the worked held sample, the state read and its 60 second repeat, the info burst and its own 15 second repeat, slot 0's three info reads before slot 1, a module reset that reads the state again, busy retries, off sent only when the state differs, on sent again when it is already on and again at the poll interval, a busy on refresh that waits and tries again, on and off with no `GET_FAULT`, one `GET_FAULT` per fault episode, `slow-start 0`, `none 0`, and `9 0`, a reset that publishes `none 0` without `GET_FAULT`, three busy now reads that move on to cycle, three busy fault reads that wait the info interval, control before a due info read, a fault left alone, reset followed by on when that is still desired, the 3 minute default, a configured absence cutoff, reset not extending that cutoff, an unplug that leaves now, cycle, totals, and fault unpublished, and a new identity that publishes the next samples. The tests use one pump module, plus one command addressed to slot 2 and one two-slot info order. `test_deep_modules.cpp` drives `PumpModule::update()`.

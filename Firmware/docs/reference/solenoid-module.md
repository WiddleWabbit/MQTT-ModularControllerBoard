# Solenoid reference

[Home](../home.md) · Guide: [Solenoid module](../solenoid-module.md)

Source: `lib/Logic/Solenoid/SolenoidModule.h`, `SolenoidPoller.h`, `SolenoidMqttBridge.h`. Shared framing is in [the daughter contract](../daughter/contract.md).

## Purpose

Type `0x0100` drives up to 16 outputs. The host learns the count when the module is identified, reads each output, and publishes the state. `{id}/solenoids` names the desired on/off state of every output. `SET_SOLENOID` is sent only where the known state differs.

The host sends these commands only while the slot is `Online`, the type is `0x0100`, and the protocol version is 1. `ECHO` is not part of this type.

## Classes

`SolenoidModule::update()` runs `SolenoidPoller`, then `SolenoidMqttBridge`. The constructor registers one MQTT handler. That handler serves both `{id}/solenoids` and `{id}/solenoids/connected`.

The poller decides the query. The bridge records desired state and publishes. Neither calls `I2cMaster`.

## Commands

Indexes on the wire are 0-based. MQTT slot and solenoid numbers are 1-based. Solenoid 1 is wire index 0. Module 1 is firmware slot 0, address `0x10`.

| Cmd | Name | Request payload | Ok response payload |
| --- | --- | --- | --- |
| `0x50` | GET_SOLENOID_COUNT | empty | `count` (`u8`, 0..16) |
| `0x51` | GET_SOLENOID_STATE | `index` (`u8`) | `index`, `state` (`u8`) |
| `0x52` | SET_SOLENOID | `index`, `desired` (`u8`, 0 or 1) | `index`, `state` (`u8`) |

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | off | Output is off |
| 1 | on | Output is on |
| 2 | disconnected | Nothing is connected to that output |

`SET_SOLENOID` accepts only off or on. A disconnected output stays disconnected and reports state 2. The host does not send `SET_SOLENOID` to an output it already knows is disconnected.

## Sequences

A first connection, and a module that is identified again, both start with `GET_SOLENOID_COUNT`. The previous count and states are forgotten. A desired-state command already accepted for that slot is applied again after the new count is known, when the new count matches the command.

After the count is known, the first state cycle starts on the following poller pass. It does not wait out the poll interval. The cycle reads outputs from index 0 upward, one `GET_SOLENOID_STATE` per pass. When the last output finishes, the next cycle is due `kSolenoidPollIntervalMs` later.

A count of 0 stores that count and does not start a state cycle.

Leaving `Online` drops the cache. A sense gap that leaves the slot `Online` keeps it.

`SolenoidPoller::update()` picks one query in this order:

1. `GET_SOLENOID_COUNT` for an online solenoid that has not reported a count. Slot 1 before slot 2. A desired-state command for a slot with no count yet makes that slot the next count query, ahead of another slot's periodic read.
2. A command step for a slot with a pending desired state. Unknown outputs are read first. `SET_SOLENOID` is then sent, lowest index first, only where the known state differs. One exchange per pass.
3. The next periodic `GET_SOLENOID_STATE`.

```text
topic:   {id}/solenoids
payload: 1 on on off off
```

That asks slot 1 for on, on, off, off. The first number is the slot, 1..4. Each following word is `on` or `off`, lowercase. The number of words must equal the count the module reported. Anything else is ignored, including a missing slot number, `ON`, a slot outside 1..4, or a different topic.

The callback records the desired state and restarts the absence window. It does not touch I2C. A desired state whose length does not match the count is not applied. A command that arrives before the count or the states are known waits through the count, then through each unknown state, then sends only the `SET` commands that still differ. A command for a slot that is still enumerating waits. A command for an empty, faulted, unsupported, or different module is dropped.

A newer command for the same slot replaces the previous desired state.

```text
solenoid 1 off, desired on  -> SET on
solenoid 2 off, desired on  -> SET on
solenoid 3 off, desired off -> no command
solenoid 4 off, desired off -> no command
```

### Connected outputs

`{id}/solenoids/connected` with payload `N` asks for the connected-output list of slot `N`. The handler records that request. It does not record desired state, and it does not restart the absence window. Extra tokens are ignored.

The reply is retained on `{id}/slot/N/solenoids`. The text is the count, then each connected index, 1-based. An index is listed when that output is `on` or `off`. A `disconnected` output is omitted. Four outputs with 1, 2, and 4 connected is `4 1 2 4`. A count of 0 is `0`. All disconnected is the count alone, such as `4`.

### Command absence

`{id}/solenoids` is expected about once a minute. If no accepted command arrives for 15 minutes, the controller turns every solenoid output off.

The window is `kSolenoidCommandTimeoutMs` in `src/main.cpp` (`15UL * 60UL * 1000UL`). It starts when the poller first runs. Each accepted command restarts it, including a command whose length later does not match the module. A malformed payload does not. When the window elapses, every online solenoid is given desired state off. Outputs that are already off or disconnected are not commanded. Outputs that are on receive `SET_SOLENOID` off, one per pass. A module that comes online after the window has elapsed is turned off once its count is known. The next accepted command restarts the window.

The window is one timer for the whole controller. Any accepted command, for any slot, restarts it.

### More than one module

Each solenoid slot has its own count, states, and poll timer. The poller still runs one query per pass. Applying a command takes the bus ahead of periodic reads, after any missing count. Unplugging one module publishes `unavailable` on that module's topics only.

## Publication

`{id}` is the device id from [Configuration](../configuration.md). A new id publishes the current output states and the connected-output list once. See the [network reference](networking.md) for the generation counter. A copy-paste publish for each topic is in the [MQTT reference](mqtt.md).

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/N` | publish, retained, QoS 0 | Slot status from `ModuleSlotPublisher`, such as `Online Solenoid addr=0x10` |
| `{id}/slot/N/solenoid/M` | publish, retained, QoS 0 | `on`, `off`, `disconnected`, or `unavailable` |
| `{id}/solenoids` | subscribe, QoS 1 | `N on off ...` |
| `{id}/solenoids/connected` | subscribe, QoS 1 | `N` |
| `{id}/slot/N/solenoids` | publish, retained, QoS 0 | count, then connected indexes, such as `4 1 2 4` |

`{id}/slot/N` is the slot snapshot. It is not a solenoid state. Outbound publishes are QoS 0. A rejected publish stays pending.

Each stored state is published, including a repeat of the same text, on the bridge pass after it was stored. The next pass, with no new state, does not publish that output again.

| Event | Topic | Payload | Retained |
| --- | --- | --- | --- |
| Periodic `GET_SOLENOID_STATE` succeeds | `{id}/slot/N/solenoid/M` | `on`, `off`, or `disconnected` | yes |
| `SET_SOLENOID` returns a state | `{id}/slot/N/solenoid/M` | `on`, `off`, or `disconnected` | yes |
| Slot is no longer an online solenoid, or the new count drops an output that had a state | `{id}/slot/N/solenoid/M` | `unavailable` | yes, once |
| Count and every output state are known, the connected set changes, or `{id}/solenoids/connected` asks again | `{id}/slot/N/solenoids` | `4 1 2 4` | yes |
| Slot is no longer an online solenoid after an inventory was published | `{id}/slot/N/solenoids` | `unavailable` | yes, once |
| Count query, malformed command, or a command still waiting for the count | — | nothing | — |
| Bridge pass with no new state | — | nothing | — |

The inventory text is the count, then each connected index. It is compared as text. An unchanged inventory is not sent again unless `{id}/solenoids/connected` asked.

A failed periodic step publishes nothing for that output. The last retained payload stays until a later read or set succeeds, or until the output is gone.

### Example

Slot 1, four outputs, all off. Then `1 on on off off`:

```text
I2C  GET_SOLENOID_COUNT
I2C  GET_SOLENOID_STATE index 0
MQTT {id}/slot/1/solenoid/1  retained  "off"
I2C  GET_SOLENOID_STATE index 1
MQTT {id}/slot/1/solenoid/2  retained  "off"
I2C  GET_SOLENOID_STATE index 2
MQTT {id}/slot/1/solenoid/3  retained  "off"
I2C  GET_SOLENOID_STATE index 3
MQTT {id}/slot/1/solenoid/4  retained  "off"

MQTT {id}/solenoids  payload "1 on on off off"
I2C  SET_SOLENOID index 0 on
MQTT {id}/slot/1/solenoid/1  retained  "on"
I2C  SET_SOLENOID index 1 on
MQTT {id}/slot/1/solenoid/2  retained  "on"
```

About one poll interval after the last state read, the four reads run again and publish again, even when the text is unchanged. If no accepted command arrives for `kSolenoidCommandTimeoutMs`, outputs that are on are commanded off.

## Errors

An index outside the reported count is `BadLength`. The host treats a count above 16, a state byte other than 0, 1, or 2, or a mismatched index as a failed query. `Busy` repeats the same command. A busy solenoid query does not, by itself, move the slot to `Fault`.

`Busy` or a failed query is tried up to three times for that step. The cycle then moves on. Three failed count queries wait `kSolenoidPollIntervalMs`. A failed on or off is tried up to three times. A later state poll that still shows an output on, while the absence cutoff is in effect, tries off again.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kSolenoidPollIntervalMs` | 60 seconds | Gap after a finished state cycle, and the wait after three failed count queries |
| `kSolenoidCommandTimeoutMs` | 15 minutes | Absence window that turns every output off |

A zero argument selects the poller's own default for that argument. It does not disable the poll or the cutoff.

## Tests

`test/test_desktop/test_solenoid.cpp` covers the three command frames, host rejection unless the slot is an online solenoid, count-then-poll ordering, the 60 second repeat, a module reset that reads the count again, a count of 0, busy retries, a desired state that sets only the outputs that differ, disconnected outputs left uncommanded, the 15 minute default, a configured absence cutoff, and retained `unavailable` after unplug. The tests use one solenoid module. `test_deep_modules.cpp` drives `SolenoidModule::update()`.

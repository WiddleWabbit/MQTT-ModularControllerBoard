# Module bus reference

[Home](../home.md) · Guide: [Module bus](../module-bus.md)

Source: `lib/Logic/Bus/ModuleBus.h`, `ModuleHost.h`, `SlotController.h`, `ModuleSlotPublisher.h`. The bytes a daughter board answers are in [the daughter contract](../daughter/contract.md) and `lib/Interfaces/ModuleProtocol.h`.

## Purpose

`ModuleBus::update()` runs the host, then the slot publisher. `SlotController` holds one slot. `ModuleHost` owns the four controllers and is the Logic class that calls `I2cMaster`. `ModuleSlotPublisher` turns the public snapshot into retained MQTT text.

## Who holds what

```text
ModuleBus
  owns ModuleHost and ModuleSlotPublisher
  update() = host.update(), then publisher.update()
  begin(), quiesce(), and resume() forward to the host

ModuleHost
  owns SlotController _slots[4]
  owns the enumeration lock (_lockOwner, -1 when free)
  owns the health cursor (_nextHealthSlot)
  readers forward to the matching controller

SlotController
  owns one slot's pins, private phase, and identity fields
  proposes a SlotI2cProposal
  applies the ModuleStepResult the host brings back

ModuleSlotPublisher
  owns the last broker-accepted text, a success flag, and the topic generation
  reads the host; the slot phase stays on the controller
```

Fields on one `SlotController`:

| Field | Meaning |
| --- | --- |
| `_phase` | Private phase. `state()` is the public fold below. |
| `_phaseBeforeAbsent` | Phase to restore if SENSE returns before the absence debounce ends. |
| `_address` | Assigned 7-bit address. Stays 0 until the ping at that address succeeds. |
| `_committedAddress` | Address this slot is using. Set by `SET_ADDRESS`, or by a ping of the slot address after `0x0A` NACKed. Kept when recovery forgets the identity. Cleared when the slot returns to `Empty`. |
| `_priorPublic` | `Online` or `Unsupported` to restore when health recovery finds the assigned address still answering. |
| `_typeId`, `_protocolVersion`, `_firmwareVersion` | Last successful `GET_IDENTITY`. Cleared on return to `Empty`, and when a recovery ping at `0x0A` succeeds. |
| `_identityEpoch` | Count of successful `GET_IDENTITY` results for this slot, including after a restart. |
| `_fault` | Last classified failure, or `None`. |
| `_lockHeld`, `_attempts`, `_healthFails` | Lock flag, tries in the current step, and failed health pings. |
| `_phaseStartedAt`, `_retryAt`, `_lastHealthAt` | Timers for debounce, boot, settle, fault wait, retry gap, and health. |

`targetAddress()` is `0x10 + slotIndex` (slot 1 is `0x10`). `typeName()` is the `kModuleTypes` name when `state()` is `Online` and the protocol is 1, and null otherwise.

Those fields live on the controller. `ModuleHost::state(slot)`, `address(slot)`, and the other readers call that controller. An index outside 0..3 reads as `Empty`, address 0, fault `None`, or a null name.

`ModuleSlotPublisher` keeps `_published[4]` (the last accepted body), `_publishedOk[4]`, and `_seenGeneration`. A failed publish leaves `_publishedOk` false, so the next `update()` tries the same text again. A new topic generation clears the accepted texts.

## Classes

`ModuleBus` exposes `begin`, `update`, `quiesce`, and `resume`. Type modules and the console reach `ModuleHost` through `host()`. `main.cpp` does not call `host()`.

`ModuleHost` exposes `begin`, `update`, `quiesce`, `resume`, the per-slot readers, `enumLockOwner`, `ping`, `echo`, and `exchange`. `exchange` is how a type module sends one command after the bus `update()` has returned. `loop()` drives the slots through `update()`. `ping` and `echo` are direct queries for a caller outside that pass. The publisher calls neither.

`ModuleSlotPublisher::update()` syncs the topic generation, formats each slot with `writeSlotStatusBody`, and publishes when that text is not the last accepted payload. The steps are under [Slot publication](#slot-publication).

One host `update()`, once `begin()` has run and the host is not quiesced:

1. Every controller runs `updatePinsAndState()`. That reads SENSE, debounce, boot wait, MOD settle, and the fault wait. It performs no I2C.
2. `_reconcileLock()` drops the lock when the owner no longer wants it, then grants it to the lowest slot whose `wantsEnumLock()` is true.
3. If the owner proposes an operation other than `None` and other than `HealthPing`, that proposal is the transaction.
4. Otherwise the host walks the slots from `_nextHealthSlot` and takes the first `HealthPing`. The cursor then moves to the following slot.
5. With neither due, `update()` returns. The publisher still runs.
6. `_issue()` performs the one `write` (`SET_ADDRESS`) or `writeRead` (everything else) and classifies it. The controller's `applyI2cResult()` stores the outcome. `_reconcileLock()` runs again.

`Timeout` or `BusError` may call `recover()` once in that same `update()`.

The private phases that want the enumeration lock are `WaitForLock`, `SelectAssert`, `ProbeDefault`, `SetAddress`, `VerifyAssigned`, `Identify`, and the matching `Recover*` phases. `Online` and `Unsupported` propose `HealthPing` and leave the lock free. During MOD settle and the retry gap the owner holds the lock and proposes `None`, so step 4 can still send one health ping.

## Hardware

Firmware slot numbers follow schematic nets and `src/main.cpp` (`SNS1_PIN` is slot 1). Physical left-to-right on the PCB is schematic 4, then 3, then 2, then 1. There is no silkscreen slot number.

| Firmware slot | SENSE | MOD | CS |
| ---: | ---: | ---: | ---: |
| 1 | GPIO39 | GPIO40 | GPIO6 |
| 2 | GPIO41 | GPIO42 | GPIO7 |
| 3 | GPIO44 | GPIO43 | GPIO15 |
| 4 | GPIO2 | GPIO1 | GPIO16 |

Shared I2C is SDA GPIO4 and SCL GPIO5, with 4.7 kΩ pull-ups on the motherboard. Sense uses the ESP32 internal pull-up. The module ties SENSE to GND when seated. LOW means present.

MOD is the enumeration select. The host drives it low to talk to unconfigured address `0x0A`, then releases it to an input. After address assignment a type handler may reuse MOD. Daughter firmware must not require MOD low after `SET_ADDRESS`.

CS is an input with a pull-up, idle HIGH, during normal operation. The module protocol does not use SPI. An ISP session can drive slot 1 CS as AVR RESET. See [programming.md](../programming.md).

`begin()` sets SENSE to input pull-up, MOD to input, and CS to input pull-up, then starts I2C. GPIO43 and GPIO44 are UART0 pins. The ESP32 driver must call Arduino `pinMode` so that peripheral detaches.

## States

Callers see `SlotState`: `Empty`, `Debouncing`, `Enumerating`, `Online`, `Unsupported`, or `Fault`. The controller stores a finer `Phase`. `state()` maps it like this.

| Private phase | Public `SlotState` |
| --- | --- |
| `Empty` | `Empty` |
| `DebouncePresent` | `Debouncing` |
| `DebounceAbsent` | The public state of `_phaseBeforeAbsent`. A sense gap shorter than the absence debounce leaves the previous public state in place. |
| `BootWait`, `WaitForLock`, `SelectAssert`, `ProbeDefault`, `ProbeAssigned`, `SetAddress`, `VerifyAssigned`, `Identify`, `RecoverWaitLock`, `RecoverSelect`, `RecoverProbeDefault`, `RecoverProbeAssigned` | `Enumerating` |
| `Online` | `Online` |
| `Unsupported` | `Unsupported` |
| `Fault` | `Fault` |

`Debouncing` is the 50 ms presence debounce (`DebouncePresent`). After that, `BootWait` holds the slot in `Enumerating` for 200 ms before it asks for the lock. Slots debounce and boot together. A slot waits for another slot only when it needs the bus.

`GET_IDENTITY` returns a type id, a protocol version, and a firmware version. Protocol version must be 1. The type id is looked up in `kModuleTypes` in `lib/Logic/Bus/SlotController.cpp`. Bands for later boards are listed in [Add a new type](../daughter/new-type.md).

| Identity | Public state | Action for that slot |
| --- | --- | --- |
| `0x0001` IdentityEcho, protocol 1 | `Online` | Health `PING` about once a second. Status text `Online IdentityEcho addr=0x1N`. `ECHO` exists for a caller. `loop()` does not poll it. |
| `0x0100` Solenoid, protocol 1 | `Online` | Health `PING`, plus the commands in the [solenoid reference](solenoid-module.md). Status text `Online Solenoid addr=0x1N`. |
| `0x0200` Sensor, protocol 1 | `Online` | Health `PING`, plus the commands in the [sensor reference](sensor-module.md). Status text `Online Sensor addr=0x1N`. |
| `0x0300` Pump, protocol 1 | `Online` | Health `PING`, plus the commands in the [pump reference](pump-module.md). Status text `Online Pump addr=0x1N`. |
| Any other type id, or a protocol version other than 1 | `Unsupported` | Health `PING` about once a second. Status text `Unsupported type=0xTTTT addr=0x1N`. No type-specific commands. |
| Address assignment or identify keeps failing | `Fault` | No health ping and no type-specific commands. Enumeration is tried again after 1 s. Status text `Fault Nack`, `Fault BadCrc`, `Fault BadFrame`, `Fault Timeout`, or `Fault Busy`. |

`exchange` returns `Ok` with the payload, `Busy` when the module asks for the same command again, `Failed` on a bad frame or a bus error, or `Rejected` when the slot cannot take that command. A `Busy` type-command does not, by itself, move the slot to `Fault`.

## Sequences

Every `ModuleHost::update()` reads SENSE on every slot before any I2C. LOW means present and starts that slot's own debounce. The other slots keep doing what they were doing.

### One enumeration at a time

Unconfigured modules all answer at `0x0A`, so the host selects one slot. The enumeration lock goes to the lowest waiting slot: slot 1 before slot 2, and so on. That slot drives MOD low, waits 10 ms, then:

1. `PING` at `0x0A`.
2. When that ping is answered, `SET_ADDRESS` to `0x10 + slotIndex` (slot 1 is `0x10`, slot 2 is `0x11`), then `PING` at the assigned address, then `GET_IDENTITY`.
3. When `0x0A` NACKs three times, `PING` at `0x10 + slotIndex`. An answer skips `SET_ADDRESS` and goes to `GET_IDENTITY`. Three NACKs enter `Fault`.

The slot address is calculated from the slot index. It is not written to flash. A successful ping stores it in the slot's RAM for this seating. Unplug clears that RAM. The next seating starts again at `0x0A`, which is what a module does after its own power cycle.

The lock stays with that slot until identify finishes and MOD is released. The other module remains `Enumerating` with no address yet. The lock then moves to the next waiting slot. One host transaction runs per `update()`. While the lock owner has an enumeration step due, that step is the transaction. During the 10 ms MOD settle and the 50 ms gap between retries the owner proposes nothing, and one health ping can use the pass. The health ping uses the other slot's assigned address. An unconfigured module under MOD answers at `0x0A`. A module that kept its slot address answers that address.

A seated module that answers neither address is pinged three times at `0x0A` and three times at the slot address, about 50 ms apart, then waits 1 s in `Fault` and repeats from `0x0A`. SENSE can still read present. Those NACKs are not printed by the I2C driver. The console prints `Slot N: Nack` once for that fault. See the [serial console reference](serial-console.md).

### Health

Each online or unsupported slot has its own one-second health timer. The host sends one due health ping per pass and rotates through the slots. Health pings do not take the enumeration lock. Three failed health pings start recovery on that slot. The other slots stay as they are. A sense gap shorter than the 50 ms absence debounce leaves the slot in its current public state.

`Timeout` or `BusError` may call `recover()` once in that same `update()`.

### Two modules plugged in together

A sensor in slot 1 and an IdentityEcho in slot 2:

```text
both slots   Debouncing, then Enumerating, over the same time
slot 1       holds the lock: PING 0x0A, SET_ADDRESS 0x10, PING 0x10, GET_IDENTITY
             Online Sensor addr=0x10
             sensor count, then presence and a reading for each input
slot 2       waits in Enumerating while slot 1 holds the lock
             then the same steps to address 0x11
             Online IdentityEcho addr=0x11
             health pings only
```

Two sensors use that same enumeration. After a slot is online, only that slot starts its type cycle. How those cycles share a poller is described on the type page.

Unplugging slot 2 returns slot 2 to `Empty`. When slot 2 was a sensor, its sensor topics publish retained `unavailable`. Slot 1 keeps its address, its type, and its own cycle.

### Slot publication

`{id}` is the device id from [configuration.md](../configuration.md). It is `watering` until `set mqtt.prefix` is applied. The full command catalog is the [MQTT reference](mqtt.md). This publisher sends the four slot topics and nothing else.

| Topic | Direction | Payload |
| --- | --- | --- |
| `{id}/slot/1` through `{id}/slot/4` | publish, retained, QoS 0 | `writeSlotStatusBody`, for example `Empty` or `Online Sensor addr=0x10` |

`ModuleSlotPublisher` sends those topics after the host has applied this pass's result. It reads the snapshot with `writeSlotStatusBody`. The phase and the identity stay on the slot controller. Each `update()`:

1. Reads `MqttTopicLayout::generation()`. A new value clears `_published` and `_publishedOk` and stores the new generation. The current bodies go out once under the new root. Retained messages under the old prefix are left on the broker.
2. For each slot, calls `writeSlotStatusBody` into an 80-byte buffer (`kSlotStatusBodyBytes`). The body is the serial slot line without the `Slot N:` prefix.
3. Skips the slot when `_publishedOk` is set and the new body matches `_published`.
4. Otherwise publishes the body retained on `{slotPrefix}/{N}`, where firmware index 0 is topic 1. QoS is 0.
5. On success, copies the body into `_published` and sets `_publishedOk`. On failure, leaves the flag clear so the next `update()` retries.

`writeSlotStatusBody` reads `state`, and then the fields that state needs:

| Public state | Payload |
| --- | --- |
| `Online` | `Online {name} addr=0xNN`. `{name}` is `typeName()`, or `Unknown` when that is null. |
| `Unsupported` | `Unsupported type=0xTTTT addr=0xNN` |
| `Fault` | `Fault Nack`, `Fault BadCrc`, `Fault BadFrame`, `Fault Timeout`, or `Fault Busy` |
| `Empty`, `Debouncing`, `Enumerating` | That word alone |

Serial status prints one line per slot from the same function, with a `Slot N:` prefix. Each slot has its own topic. After a reconnect, an accepted body stays unpublished until the text changes.

## Configuration

Defaults in `ModuleHostConfig`:

| Field | Default | Role |
| --- | ---: | --- |
| `presenceDebounceMs` | 50 | Present and absent debounce |
| `bootWaitMs` | 200 | Wait after debounce before the first transaction |
| `modSettleMs` | 10 | MOD low before `PING` at `0x0A` |
| `i2cTimeoutMs` | 50 | Driver timeout for one transaction |
| `i2cClockHz` | 100000 | Bus clock |
| `commandRetries` | 3 | Tries before a fault wait |
| `retryGapMs` | 50 | Gap between those tries |
| `faultRetryMs` | 1000 | Wait in `Fault` before enumerating again |
| `healthPingMs` | 1000 | Per-slot health period |
| `healthFailLimit` | 3 | Failed pings before recovery |

`ModuleProtocol.h` also fixes daughter-side limits the host assumes: MOD gate `kModGateMaxMs` (5 ms) and clock stretch `kClockStretchMaxMs` (40 ms). `main.cpp` passes `ModuleHostConfig{}`, so these defaults are the product timings.

## Tests

- `test/test_desktop/test_modules.cpp` — frames, enumeration, a module that kept its slot address, the lock, faults, `begin` pin modes.
- `test/test_desktop/test_slot_publish.cpp` — retained slot text, retries, prefix generation.
- `test/test_desktop/test_deep_modules.cpp` — `ModuleBus::update()` through the public module, including quiesce.

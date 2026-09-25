# Daughter module contract

This firmware is the I2C master for four hot-pluggable slots on the
MQTT-ModularControllerBoard. Other module firmware projects copy
`lib/Interfaces/ModuleProtocol.h` and follow this document.

## Hardware

Firmware slot numbers follow schematic nets / `src/main.cpp` (`SNS1_PIN` =
Slot 1). Physical left-to-right on the PCB is schematic 4 → 1. There is no
silkscreen “Slot N”.

| Firmware slot | SENSE | MOD | CS |
| ---: | ---: | ---: | ---: |
| 1 | GPIO39 | GPIO40 | GPIO6 |
| 2 | GPIO41 | GPIO42 | GPIO7 |
| 3 | GPIO44 | GPIO43 | GPIO15 |
| 4 | GPIO2 | GPIO1 | GPIO16 |

Shared I2C: SDA GPIO4, SCL GPIO5, motherboard 4.7 kΩ pull-ups. Sense uses the
ESP32 internal pull-up; the module ties SENSE to GND when seated. LOW = present.

MOD is an open-drain enumeration select. The host drives it LOW to talk to
unconfigured address `0x0A`, then releases it to input. After address
assignment a type handler may reuse MOD. Module firmware must not require MOD
low after `SET_ADDRESS`.

CS is input-pull-up (idle HIGH). SPI is unsupported in this firmware slice.

## Addressing

- ESP32 is the sole I2C master. Modules NACK general call `0x00` and never
  become master.
- Unconfigured address: `0x0A`. ACK `0x0A` only while MOD is LOW, never as a
  reset default. Keep the I2C slave disabled until MOD is an input-pull-up.
  Enable/disable the `0x0A` listener within 5 ms of a MOD edge.
- Assigned address: `0x10 + slotIndex` (Slot 1 → `0x10` … Slot 4 → `0x13`).
  After `SET_ADDRESS` STOP, ACK the assigned address regardless of MOD.

## Frame

```text
byte 0     length     = 2 + payloadLen
byte 1     command    (request) or status (response)
bytes 2..  payload    0..16 bytes, big-endian
byte N     crc8       CRC-8/SMBus over bytes 0..N-1 (N == length)
```

Host always `writeRead`s 19 bytes. Module clocks the real frame then 0xFF pad
and ACKs extra read clocks. CRC-8/SMBus over `rx[0 .. len-1]`, compare to
`rx[len]`. Poly 0x07, init 0x00, refin/refout 0, xorout 0x00.

`SET_ADDRESS` is a **4-byte write plus STOP**, not a read:

```text
tx[0] = 0x03
tx[1] = 0x03          // kCmdSetAddress
tx[2] = newAddr       // 0x10–0x6F
tx[3] = crc8Smbus(tx, 3)
```

Commit the new address after STOP, even if MOD then goes HIGH.

## Commands

| Cmd | Name | Host txn |
| --- | --- | --- |
| 0x01 | PING | writeRead 19, empty payload |
| 0x02 | GET_IDENTITY | writeRead 19, 5-byte identity |
| 0x03 | SET_ADDRESS | write 4 bytes + STOP |
| 0x40 | ECHO | IdentityEcho only, 0..16 bytes |
| 0x41 | GET_SENSOR_COUNT | Sensor `0x0200` only, empty request, 1-byte count |
| 0x42 | GET_SENSOR_CONNECTED | Sensor `0x0200` only, 1-byte index |
| 0x43 | GET_SENSOR_READING | Sensor `0x0200` only, 1-byte index |
| 0x50 | GET_SOLENOID_COUNT | Solenoid `0x0100` only, empty request, 1-byte count |
| 0x51 | GET_SOLENOID_STATE | Solenoid `0x0100` only, 1-byte index |
| 0x52 | SET_SOLENOID | Solenoid `0x0100` only, index and off/on |
| 0x60 | GET_PUMP_STATE | Pump `0x0300` only, empty request, 1-byte state |
| 0x61 | SET_PUMP | Pump `0x0300` only, off or on |
| 0x62 | RESET_PUMP | Pump `0x0300` only, empty request, 1-byte state |

Identity payload (big-endian): typeId (u16), protocolVersion (u8),
firmwareVersion (u16). Protocol version 1 is required for `Online`.

Status bytes: Ok 0x00, BadCrc 0x01, UnknownCmd 0x02, BadLength 0x03, Busy 0x04,
Unsupported 0x05.

## Type ids

| Range | Use |
| --- | --- |
| `0x0001` | IdentityEcho |
| `0x0100` | Solenoid module |
| `0x0101–0x01FF` | Further actuator types |
| `0x0200` | Sensor module |
| `0x0201–0x02FF` | Further sensor types |
| `0x0300` | Pump module |
| `0x0301–0x03FF` | Further types in that block |
| `0xF000–0xFFFF` | Experimental |

## Several modules

The host watches all four slots on every pass. Presence, enumeration,
identify, and the action that follows are per slot. Two modules plugged in
at once each run this sequence. They share the I2C bus, so enumeration and
later bus commands take turns.

### Scan

Every `ModuleHost::update()` reads SENSE on every slot before any I2C.
LOW means present and starts that slot's own 50 ms debounce. The other slots
keep doing whatever they were doing. After debounce the slot waits 200 ms for
the module to boot. The public state is `Debouncing` during the presence
debounce, then `Enumerating` from the boot wait through identify.

Slots debounce and boot together. A slot waits for another slot only when it
needs the bus.

### One enumeration at a time

Unconfigured modules all answer at `0x0A`, so the host selects one slot.
The enumeration lock goes to the lowest waiting slot number: slot 1 before
slot 2, and so on. That slot drives MOD low, waits 10 ms, then:

1. `PING` at `0x0A`
2. `SET_ADDRESS` to `0x10 + slotIndex` (slot 1 → `0x10`, slot 2 → `0x11`)
3. `PING` at the assigned address
4. `GET_IDENTITY`

The lock stays with that slot until identify finishes and MOD is released.
The other module remains `Enumerating` with no address yet. The lock then
moves to the next waiting slot, which gets its own address. One host
transaction runs per `ModuleHost::update()`. While a slot holds the lock for
a ping, address assignment, or identify, that step is the host transaction
for the pass.

### Identify chooses the action

`GET_IDENTITY` returns a type id, a protocol version, and a firmware version.
Protocol version must be 1. The type id is looked up in a fixed table. The
bands above reserve ids for later modules. Four ids are acted on today:
IdentityEcho `0x0001`, Solenoid `0x0100`, Sensor `0x0200`, and Pump `0x0300`.
Any other identified id, including another id inside those bands, is
`Unsupported`.

| Identity | Public state | Action for that slot |
| --- | --- | --- |
| `0x0001` IdentityEcho, protocol 1 | `Online` | Health `PING` about once a second. Status text `Online IdentityEcho addr=0x1N`. `ECHO` exists for a caller. `loop()` does not poll it. |
| `0x0100` Solenoid, protocol 1 | `Online` | Health `PING` about once a second, plus the count, state, and on/off commands in [SOLENOIDMODULE.md](SOLENOIDMODULE.md). Status text `Online Solenoid addr=0x1N`. |
| `0x0200` Sensor, protocol 1 | `Online` | Health `PING` about once a second, plus the count and reading cycle in [SENSORMODULE.md](SENSORMODULE.md). Status text `Online Sensor addr=0x1N`. |
| `0x0300` Pump, protocol 1 | `Online` | Health `PING` about once a second, plus the state, on/off, and reset commands in [PUMPMODULE.md](PUMPMODULE.md). Status text `Online Pump addr=0x1N`. |
| Any other type id, or protocol version other than 1 | `Unsupported` | Health `PING` about once a second. Status text `Unsupported type=0xTTTT addr=0x1N`. No type-specific commands. |
| Address assignment or identify keeps failing | `Fault` | No health ping and no type-specific commands. Enumeration is tried again after 1 s. Status text `Fault Nack`, `Fault BadCrc`, `Fault BadFrame`, `Fault Timeout`, or `Fault Busy`. |

Each online or unsupported slot has its own one-second health timer. The
host sends one due health ping per pass and rotates through the slots. Three
failed health pings start recovery on that slot. The other slots stay as
they are. A sense gap shorter than the 50 ms absence debounce leaves the
slot in its current public state.

`watering/slot/N` is published when that slot's status text changes. Serial
status prints one line per slot. One module's line does not replace the
other's topic.

### Two modules plugged in together

A Sensor module in slot 1 and an IdentityEcho module in slot 2:

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

Two Sensor modules use that same enumeration. After a slot is online, only
that slot starts the sensor cycle. How those cycles share the poller is
described in [SENSORMODULE.md](SENSORMODULE.md).

Unplugging slot 2 returns slot 2 to `Empty`. When slot 2 was a Sensor module,
its sensor topics publish retained `unavailable`. Slot 1 keeps its address,
its type, and its own cycle.

## Sensor module (`0x0200`)

The host sends these commands only after identify reports type `0x0200` and
protocol version 1. Sensor indexes on the wire are 0-based. A module reports
at most 16 inputs. Poll timing and MQTT publication are described in
[SENSORMODULE.md](SENSORMODULE.md).

| Command | Request payload | Ok response payload |
| --- | --- | --- |
| GET_SENSOR_COUNT | empty | `count` (`u8`, 0..16) |
| GET_SENSOR_CONNECTED | `index` (`u8`) | `index`, `connected` (`u8`, 0 or 1) |
| GET_SENSOR_READING | `index` (`u8`) | `index`, `connected`, `value` (`i32` big-endian) |

`value` is a raw module unit. The host does not scale it. An index outside
the reported count is `BadLength`. `Busy` means try the same command again.
A count above 16, a connected byte other than 0 or 1, or a mismatched index
is a bad frame.

## Solenoid module (`0x0100`)

The host sends these commands only after identify reports type `0x0100` and
protocol version 1. Solenoid indexes on the wire are 0-based. A module
reports at most 16 outputs. State bytes are off `0`, on `1`, and disconnected
`2`. `SET_SOLENOID` carries off or on only. Poll timing, the MQTT desired
state, and the command-absence cutoff are described in
[SOLENOIDMODULE.md](SOLENOIDMODULE.md). `kSolenoidCommandTimeoutMs` in
`src/main.cpp` is 15 minutes: that long without an accepted
`watering/solenoids` command turns every output off.

| Command | Request payload | Ok response payload |
| --- | --- | --- |
| GET_SOLENOID_COUNT | empty | `count` (`u8`, 0..16) |
| GET_SOLENOID_STATE | `index` (`u8`) | `index`, `state` (`u8`) |
| SET_SOLENOID | `index`, `desired` (`u8`, 0 or 1) | `index`, `state` (`u8`) |

An index outside the reported count is `BadLength`. `Busy` means try the
same command again. A count above 16, a state byte other than 0, 1, or 2,
or a mismatched index is a bad frame.

## Pump module (`0x0300`)

The host sends these commands only after identify reports type `0x0300` and
protocol version 1. One module drives one pump. State bytes are off `0`,
on `1`, and fault `2`. `SET_PUMP` carries off or on only. `RESET_PUMP` is
sent only after an MQTT reset. Poll timing, the MQTT desired state, and the
command-absence cutoff are described in [PUMPMODULE.md](PUMPMODULE.md).
`kPumpCommandTimeoutMs` in `src/main.cpp` is 3 minutes: that long without an
accepted `watering/pump` on/off command turns a pump that is on off. A reset
does not refresh that window.

| Command | Request payload | Ok response payload |
| --- | --- | --- |
| GET_PUMP_STATE | empty | `state` (`u8`) |
| SET_PUMP | `desired` (`u8`, 0 or 1) | `state` (`u8`) |
| RESET_PUMP | empty | `state` (`u8`) |

`Busy` means try the same command again. A state byte other than 0, 1, or 2
is a bad frame.

## Timing

I2C 100 kHz. Clock stretch ≤ 40 ms. Host allows 200 ms after present debounce
before the first transaction. Host MOD settle is 10 ms.

## Compliance checklist

- SENSE to GND when seated
- MOD pull-up; I2C slave disabled until MOD is input-pull-up
- Never ACK `0x0A` as a reset default
- ACK `0x0A` only while MOD is LOW; gate ≤ 5 ms
- 7-bit addresses; NACK `0x00`; not an I2C master
- `SET_ADDRESS` is a 4-byte write + STOP; commit after STOP
- Repeated-start `writeRead` for PING / IDENTITY / ECHO
- Response padded to 19 bytes with 0xFF
- `PING` and `GET_IDENTITY`; `protocolVersion == 1`
- Unique `typeId` from the table above

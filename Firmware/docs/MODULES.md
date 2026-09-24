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

Identity payload (big-endian): typeId (u16), protocolVersion (u8),
firmwareVersion (u16). Protocol version 1 is required for `Online`.

Status bytes: Ok 0x00, BadCrc 0x01, UnknownCmd 0x02, BadLength 0x03, Busy 0x04,
Unsupported 0x05.

## Type ids

| Range | Use |
| --- | --- |
| `0x0001` | IdentityEcho |
| `0x0100–0x01FF` | Actuators |
| `0x0200` | Sensor module |
| `0x0201–0x02FF` | Further sensor types |
| `0xF000–0xFFFF` | Experimental |

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

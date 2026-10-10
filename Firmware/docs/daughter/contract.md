# What a daughter module must do

[Home](../home.md)

The controller is the only I2C master. Four slots share SDA and SCL. Your board sits in one slot and answers the frames below. Copy `lib/Interfaces/ModuleProtocol.h` for the command bytes and `crc8Smbus()`.

If this page and the header disagree on a byte value, the header is the one the controller compiles.

Worked bytes are in [Frames](frames.md). Choosing a type id, and the controller-side module that talks to a new board, are in [Add a new type](new-type.md). How the host walks the slots is in the [module bus reference](../reference/module-bus.md).

## You must

- Tie SENSE to GND while the board is seated. The controller's pull-up reads LOW as present.
- Leave the I2C slave off until MOD is released. The controller uses MOD as an input with a pull-up when it is not selecting you.
- ACK `0x0A` only while MOD is low, within 5 ms of that edge (`kModGateMaxMs`). Never ACK `0x0A` as a reset default.
- NACK general call `0x00`. Do not become a master.
- On `SET_ADDRESS`, take the new address after STOP, then ACK it with MOD in either state. The controller will not hold MOD low after that.
- Answer `PING` and `GET_IDENTITY`. `GET_IDENTITY` returns type id (`u16`), protocol version (`u8`), and firmware version (`u16`), big-endian. Protocol version is 1.
- Use a type id from [Add a new type](new-type.md).
- Reply to a repeated-start read with the real frame, then `0xFF` until 19 bytes have been clocked. ACK the extra clocks.
- Compute CRC-8/SMBus over every frame byte except the CRC byte. Polynomial `0x07`, init `0x00`, no reflection, xor-out `0x00`. The check value for the ASCII bytes `123456789` is `0xF4`.

## You must not

- Drive SDA or SCL except as an open-drain slave.
- Treat MOD as part of normal operation after your address is assigned.
- Answer another type's commands. An unknown command returns status `UnknownCmd`.

## Frame

```text
byte 0     length    = 2 + payload length
byte 1     command on the way in, status on the way out
bytes 2..  payload   0 to 16 bytes, big-endian
last byte  crc8      over bytes 0 .. length-1
```

The host always `writeRead`s 19 bytes. It checks the CRC at `rx[length]` and ignores the pad.

Status bytes:

| Value | Name | Meaning |
| ---: | --- | --- |
| `0x00` | Ok | Payload follows |
| `0x01` | BadCrc | Request CRC did not match |
| `0x02` | UnknownCmd | This type does not implement that command |
| `0x03` | BadLength | Index or payload length is wrong |
| `0x04` | Busy | Same command will be sent again |
| `0x05` | Unsupported | Command is known and refused |

## Shared commands

| Cmd | Name | What the host sends |
| --- | --- | --- |
| `0x01` | PING | `writeRead` 19, empty payload |
| `0x02` | GET_IDENTITY | `writeRead` 19, empty payload. Ok payload is 5 bytes |
| `0x03` | SET_ADDRESS | 4-byte write plus STOP. No read |
| `0x40` | ECHO | IdentityEcho `0x0001` only. Other types return `UnknownCmd` |

Identity payload, big-endian:

```text
type id hi, type id lo, protocol version, firmware hi, firmware lo
```

An Ok identity frame has length byte `0x07` (2 + 5). Any other Ok length is a bad frame.

`SET_ADDRESS` is a 4-byte write and a STOP, not a read:

```text
03 03 <addr> <crc>
```

`<addr>` is `0x10` plus the slot index. Slot 1 is `0x10`, slot 4 is `0x13`. Commit that address after STOP, even if MOD then goes high. Assigned addresses the host will use are `0x10` through `0x13`. The header allows `0x10` through `0x6F`.

## Timing the host will use

The bus clock is 100 kHz. Stretch the clock no longer than 40 ms (`kClockStretchMaxMs`).

After SENSE has been low through the 50 ms debounce, the host waits 200 ms (`bootWaitMs`) before the first transaction. It then holds MOD low for 10 ms (`modSettleMs`) and pings `0x0A`. Be answering by the end of that boot wait. The full host table is in the [module bus reference](../reference/module-bus.md).

## After you are online

The controller health-pings about once a second. Your type page says which further commands arrive, and in what order:

- Sensor `0x0200` — [Sensor reference](../reference/sensor-module.md)
- Solenoid `0x0100` — [Solenoid reference](../reference/solenoid-module.md)
- Pump `0x0300` — [Pump reference](../reference/pump-module.md)

Wire indexes on those pages are 0-based. MQTT slot and channel numbers are 1-based.

The module protocol does not use SPI. During programming the selected slot's CS pin can be taken as AVR RESET or as the UPDI wire. See [programming.md](../programming.md). Motherboard GPIO numbers for SENSE, MOD, and CS are in the [module bus reference](../reference/module-bus.md).

## Checklist

- SENSE to GND when seated
- I2C slave disabled until MOD is an input with a pull-up
- Never ACK `0x0A` as a reset default
- ACK `0x0A` only while MOD is low, within 5 ms
- 7-bit addresses, NACK `0x00`, not an I2C master
- `SET_ADDRESS` is a 4-byte write plus STOP, committed after STOP
- Repeated-start `writeRead` for `PING` and `GET_IDENTITY`
- Response padded to 19 bytes with `0xFF`
- `protocolVersion == 1`
- Type id from [Add a new type](new-type.md), and only that type's commands

# Frames

[Home](../home.md) · [What a daughter module must do](contract.md)

Bytes below were produced with `crc8Smbus()` in `lib/Interfaces/ModuleProtocol.h`. A CRC implementation is ready to use here when the ASCII bytes `123456789` return `0xF4`.

The write side is the bytes the host clocks out. A read is a repeated start, then 19 clocks. The module clocks its frame and fills the rest with `0xFF`. The host stops at the CRC byte named by the length field.

## PING

Host write, empty payload:

```text
02 01 2D
```

Ok reply, empty payload. Length `0x02`, status Ok `0x00`, CRC `0x2A`:

```text
02 00 2A FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

## SET_ADDRESS

Assign slot 1, address `0x10`. This is a write of four bytes and a STOP. There is no read. Commit `0x10` after STOP.

```text
03 03 10 F2
```

`0x03` is the length (2 + 1 address byte) and also the `SET_ADDRESS` command. The I2C address is not a fifth frame byte.

## GET_IDENTITY

Host write, empty payload:

```text
02 02 24
```

Ok reply for IdentityEcho `0x0001`, protocol 1, firmware `0x1234`. That firmware word is the sample used by `testCodecDecodesIdentityBigEndian`. A real board uses its own firmware version. Length `0x07` means the CRC is byte 7:

```text
07 00 00 01 01 12 34 9F FF FF FF FF FF FF FF FF FF FF FF
```

| Byte | Value | Meaning |
| ---: | ---: | --- |
| 0 | `07` | Length, CRC sits at this index |
| 1 | `00` | Ok |
| 2 | `00` | Type id high |
| 3 | `01` | Type id low (`0x0001`) |
| 4 | `01` | Protocol version |
| 5 | `12` | Firmware high |
| 6 | `34` | Firmware low |
| 7 | `9F` | CRC-8/SMBus over bytes 0..6 |

A sensor `0x0200`, protocol 1, firmware `0x0001` is the same shape with bytes `07 00 02 00 01 00 01` and CRC `0xBB`.

## One type command

`GET_SENSOR_COUNT` (`0x41`) is an empty request, same shape as `PING`:

```text
02 41 EA
```

Ok reply, count = 2. Length `0x03`, status Ok, payload `0x02`, CRC `0xB3`:

```text
03 00 02 B3 FF FF FF FF FF FF FF FF FF FF FF FF FF
```

Sensor, solenoid, and pump payload layouts are on their reference pages: [Sensor](../reference/sensor-module.md), [Solenoid](../reference/solenoid-module.md), [Pump](../reference/pump-module.md).

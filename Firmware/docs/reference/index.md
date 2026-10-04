# Reference

[Home](../home.md)

These pages are the technical wiki: states, bytes, errors, timing, and the desktop test that covers them. The short pages one level up say who creates what and what one `update()` does.

## Bus

- [Module bus](module-bus.md) — who holds slot state, the lock, faults, and slot publication.
- [Daughter contract](../daughter/contract.md) — what a board in a slot must answer. Frames are in [daughter/frames.md](../daughter/frames.md).

## Daughter types

- [Sensor](sensor-module.md) — type `0x0200`. Count, presence, readings, immediate read.
- [Solenoid](solenoid-module.md) — type `0x0100`. Count, state, desired on/off, absence cutoff.
- [Pump](pump-module.md) — type `0x0300`. State, on/off, reset, absence cutoff.

## Network and console

- [Network](networking.md) — Wi-Fi, NTP, MQTT, the generation counter, `INetworkConfigStore`.
- [MQTT](mqtt.md) — every subscription and publication, the payload text, and a line to publish when testing.
- [Configuration](configuration.md) — stored fields, `set` / `apply`, topic root.
- [Serial console](serial-console.md) — the seven-line snapshot and plug gating.
- [Programming](programming.md) — slot 1 ISP session, STK500, and the RTC latch.

## Ports

- [Hardware interfaces](../interfaces.md) — each port, and what a fake must support.
- `INetworkConfigStore` is documented with [Network](networking.md). It is not a hardware port.

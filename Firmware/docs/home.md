# Firmware

The controller is seven modules. `src/main.cpp` constructs them and calls
`update()`. It does not run the steps inside a module.

Start here. Three ways to read.

## How the controller runs

Each of these is a short page: what the module creates, and what one `update()` does.

1. [Architecture](architecture.md) — the loop, and who owns whom.
2. [Module bus](module-bus.md) — `SlotController` holds each slot; the host does one I2C transaction; the publisher sends slot status.
3. [Sensor](sensor-module.md), [Solenoid](solenoid-module.md), and [Pump](pump-module.md) — one daughter type each.
4. [Network](networking.md) — Wi-Fi, then NTP, then MQTT.
5. [Serial console](serial-console.md) — USB lines, then the snapshot.
6. [Configuration](configuration.md) — `set` stages, `apply` stores, and the three names.
7. [Programming](programming.md) — slot 1 as Arduino as ISP.
8. [Hardware ports](interfaces.md) — the interfaces Logic calls.

## When you need a byte, a state, or a test

The [reference index](reference/index.md) is the technical wiki. Each module page there has the commands, the sequences, the errors, and the desktop test that covers them.

## Building a daughter module

A daughter module is the board in a slot. That is separate from the classes inside a controller module.

1. [What a daughter module must do](daughter/contract.md).
2. [Frames, with worked bytes](daughter/frames.md).
3. [Add a new type](daughter/new-type.md) — the board, and the controller module that talks to it.

`lib/Interfaces/ModuleProtocol.h` is the command list daughter projects copy. If a byte on these pages and the header disagree, the header is the one the controller compiles.

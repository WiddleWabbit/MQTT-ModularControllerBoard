# Add a new type

[Home](../home.md) · [What a daughter module must do](contract.md)

A new daughter board needs two things: firmware on the board that answers the contract, and a controller module that polls it and publishes it. Sensor (`lib/Logic/Sensor/`) is the pattern to copy. Solenoid and pump are the same shape.

## Type id

Pick an unused id. The range reserves later boards of one kind.

| Range | Use |
| --- | --- |
| `0x0001` | IdentityEcho. Health ping only. `loop()` does not poll `ECHO`. |
| `0x0100` | Solenoid module. Taken. |
| `0x0101–0x01FF` | Further actuator types |
| `0x0200` | Sensor module. Taken. |
| `0x0201–0x02FF` | Further sensor types |
| `0x0300` | Pump module. Taken. |
| `0x0301–0x03FF` | Further types in the pump block |
| `0xF000–0xFFFF` | Experimental |

An id that is not listed in `kModuleTypes` comes online as `Unsupported`. The host health-pings it and sends no type commands. Protocol version 2 of a known id does the same.

## On the board

1. Implement [the contract](contract.md): SENSE, MOD, `0x0A`, `SET_ADDRESS`, `PING`, `GET_IDENTITY`, the 19-byte padded read, and CRC-8/SMBus.
2. Report your type id and protocol version 1 from `GET_IDENTITY`.
3. Implement only your commands. Return `UnknownCmd` for the others. Return `Busy` when the result is not ready. The host will repeat that command.
4. Keep wire indexes 0-based. Report at most 16 channels when the command is indexed. Check a frame against [Frames](frames.md) before looking at a logic analyser.
5. Copy `lib/Interfaces/ModuleProtocol.h` into the daughter project, and add your command bytes there in the controller project in the same change.

Sensor and solenoid boards are an ATmega328PB programmed from slot 1. The pump is an ATtiny1614 on UPDI, which this controller does not program yet. See [programming.md](../programming.md).

## On the controller

Follow `lib/Logic/Sensor/`. Four pieces, owned by the module. `main.cpp` does not call the poller or the bridge.

| Piece | Job |
| --- | --- |
| Commands | Encode and decode your payloads. |
| Poller | Per-slot results. At most one `ModuleHost::exchange()` per `update()`. |
| Bridge | Publish from what the poller stored. The MQTT callback only records a request. |
| Module | Owns the poller and the bridge. `update()` runs the poller, then the bridge. |

Then:

1. Add the type id constant next to `kTypeSensorModule` in `ModuleProtocol.h`.
2. Add a row to `kModuleTypes` in `lib/Logic/Bus/SlotController.cpp`. The name in that row is the word in `Online <name> addr=0x1N`. Without the row, the slot stays `Unsupported` and `exchange()` rejects the command.
3. Register an inbound handler in the module constructor when the type has a command topic. Construct the module in `src/main.cpp` before `Network::begin()`. Global constructors already run before `setup()`, which is what the existing three types rely on.
4. Call `update()` from `loop()` after `moduleBus.update()`. Put the poll interval, and any command-absence cutoff, in the timing block of `main.cpp` and pass them in. A zero interval selects the poller's own default. It does not disable the poll.
5. Add the command topic on `MqttTopicLayout` if the broker must deliver one. The layout subscribes to five topics today, all in use (`kSubscriptionCount`). `Network` accepts four handlers (`kMaxInboundHandlers`), and three are in use. A new inbound topic means raising the subscription count. A fourth handler still fits. One handler may serve more than one topic. The sensor bridge and the solenoid bridge do.
6. Add a desktop test that drives the module through its public `update()`, next to `test/test_desktop/test_sensor.cpp`. Keep any lower-level tests that cover frame edges.
7. Add a short guide next to `docs/sensor-module.md` and a reference page under `docs/reference/`. Link the guide from `docs/home.md` and the reference from `docs/reference/index.md`. State the interval and any absence window on the guide. Name the `main.cpp` constants on the reference page.

`ModuleHost` stays the only I2C caller. The new poller asks it for one exchange. It does not open the bus itself.

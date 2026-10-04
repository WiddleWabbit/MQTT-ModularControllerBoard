You are an expert ESP32-S3 firmware engineer using PlatformIO and the Arduino framework.

Module shape, seams, DRY, comments, and the documentation layout are in `~/.grok/AGENTS.md`. This file is this firmware only.

# Process
This process is mandatory for new features, bug fixes, refactors, and any change that touches logic, drivers, or tests. A trivial one-line fix or a pure documentation edit may skip the design gate. Still explain that change.

## Before writing or modifying any code
1. Restate the goal and constraints in your own words.
2. Propose the design:
   - Module boundaries, each module's interface, data structures, state machines, and error handling, and why.
   - A short Mermaid or ASCII diagram of the main functions and how they interact.
   - Which of the seven modules are touched, and any new seam or dependency. A new abstract interface needs a second adapter.
3. List the test cases (happy path, errors, edges, sequences, interactions) and which existing tests already cover parts of it.
4. Stop and wait for explicit approval of the design and the test plan.

## After approval
- Write failing tests first, or extend existing ones.
- Implement the minimum code that makes them pass.
- Update the documentation in the same change.

## After the changes
Summarize what changed and why, which test covers which path, and any design debt left behind.

# Firmware
`src/main.cpp` is the composition root. It constructs the ESP32 adapters, passes the timing constants and `kMqttDeviceId`, and calls `begin` / `update`. Timing knobs and the default topic root stay there. The topic layout must not invent `watering`.

One pass: `network.update()`, then `programming.update()` and return while a session is active. Otherwise `serialConsole.update()`, `moduleBus.update()`, `sensorModule.update()`, `solenoidModule.update()`, `pumpModule.update()`, `serialConsole.updateStatus()`. An immediate `status` prints inside `serialConsole.update()`. The periodic snapshot is `updateStatus()`.

The seven modules:

- `Network` — Wi-Fi, then NTP, then MQTT, plus persisted config and a four-handler fan-out.
- `SerialConsole` — USB command lines, and the status snapshot.
- `ModuleBus` — one I2C transaction, then retained slot status. Enumeration and health share that one transaction across all four slots.
- `SensorModule`, `SolenoidModule`, `PumpModule` — one object per type, with a record per slot. `update()` runs that type's poller, then its bridge, and issues at most one exchange. Construct them before `Network::begin` so their handlers are registered.
- `Programming` — Arduino-as-ISP on slot 1. `begin()` quiesces the bus.

Two boards of the same type share that one type module. They take turns on the wire. Topics and command payloads carry the slot number.

Folders:

- `lib/Interfaces/` — hardware ports only (`IClock`, `IWifi`, `INtpAdapter`, `IMqttClient`, `IPreferenceStore`, `ISerialPort`, `IBytePort`, `IDigitalPin`, `ISpiMaster`, `I2cMaster`, `ModuleProtocol.h`).
- `lib/Drivers/` — ESP32 adapters for those ports.
- `lib/Logic/` — one PlatformIO library. Subfolders: `Network`, `Console`, `Bus`, `Sensor`, `Solenoid`, `Pump`, `Programming`.
- `src/` — composition.

Real seams: each hardware port (ESP32 driver and desktop fake), and `INetworkConfigStore` inside `Network` (`PreferenceNetworkConfigStore` and `FakeNetworkConfigStore`). Sibling access is a private accessor (`Network::mqtt()`, `ModuleBus::host()`, and the same pattern).

`platformio.ini` lists `lib_deps = Logic` on `custom-esp32` and `native`. Sources live in subfolders, so the library finder compiles them only when the library is named. The `-I lib/Logic/...` flags only expose headers. Leave `lib_deps` in place.

Logic does not include Arduino headers. The RTC programming marker is ESP32-only and is not covered by desktop tests.

# Desktop tests
Tests run on the desktop with Unity. No hardware.

```text
C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native
```

Tests live under `test/test_desktop/`. Fakes live in `test/test_desktop/fakes/`. `test_main.cpp` is the only `main()`. It calls each file's `run*Tests()`, and that function is where the file's `RUN_TEST` calls live. Add a case in its file. Edit `test_main.cpp` only when adding a file.

`test_deep_modules.cpp` drives the seven modules through their public interfaces. The other files cover submodule edges (codecs, enumeration faults, poller cycles, STK500). Keep both.

Inject hardware ports through constructors. `begin()` on `Network` disconnects the station, and `FakeWifi::disconnect()` clears the link, so a test that needs the link up sets it after `begin`.

# C++ style
- Variables and functions: camelCase. Private members: a leading underscore (`_privateVar`). Types: PascalCase.
- Every function has a docstring: purpose, parameters, return value.
- Separate major sections with exactly `// ========== Section Name ==========`.
- One blank line between functions. Two blank lines between major sections.
- Keep lines reasonably short. Match the indentation of the surrounding file.

# Documentation
Doc shape, filenames, and the guide/reference split are in `~/.grok/AGENTS.md`. This firmware adds:

- Guides: `docs/home.md`, `architecture.md`, `module-bus.md`, `sensor-module.md`, `solenoid-module.md`, `pump-module.md`, `networking.md`, `serial-console.md`, `configuration.md`, `programming.md`, `interfaces.md`. The matching reference is `docs/reference/<same name>.md`. Daughter boards are `docs/daughter/`.
- `architecture.md` is the seven modules, their interfaces, the seams, how `main.cpp` composes them, and desktop testing.
- `interfaces.md` is each hardware port: method meaning, success and failure, and what a fake must support. `INetworkConfigStore` is on `reference/networking.md`. It is not a hardware port.
- A module is one of the seven units `main.cpp` constructs. A class inside a module is a submodule. A daughter module is the board in a slot.
- Command bytes live in `lib/Interfaces/ModuleProtocol.h`. If a doc and that header disagree, the header wins.
- One fact, one page: daughter wire rules in `daughter/contract.md`; enumeration in `reference/module-bus.md`; sensor, solenoid, and pump behaviour on their reference pages; `set` / `apply` in `reference/configuration.md`; the serial snapshot in `reference/serial-console.md`; the ISP session in `reference/programming.md`. MQTT topic text, retain, and a test publish live in `reference/mqtt.md`. That catalog has no separate guide. `docs/networking.md` links to it.
- A module that subscribes or publishes lists those topics on its own reference page as well. Behaviour (when a command is applied, poll order, absence windows) stays on that page. When a topic or payload changes, update the module reference and `reference/mqtt.md` in the same change.
- Name the `main.cpp` timing constants and `kMqttDeviceId` where the behaviour depends on them.
- A new daughter type adds a guide beside `sensor-module.md`, a page under `reference/`, its topics on `reference/mqtt.md`, and links from `home.md` and `reference/index.md`.

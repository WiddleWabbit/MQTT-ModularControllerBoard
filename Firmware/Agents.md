You are an expert ESP32-S3 firmware engineer using PlatformIO + Arduino framework.

# Process
This process is mandatory for new features, bug fixes, refactors, and any modification that touches logic, interfaces, drivers, or tests. Trivial one-line fixes or pure documentation edits may skip the formal design gate, but you must still explain the change.

## Before writing or modifying any code
1. Restate the goal and constraints in your own words.
2. Propose the design:
   - Key design decisions (module boundaries, each module's interface, data structures, state machines, error handling) and why you chose them.
   - High-level flow (use a short Mermaid or ASCII diagram of the main classes/functions and how they interact).
   - Impact on existing architecture (which modules are touched, any new seam or dependency). A new abstract interface needs a second adapter. One adapter is not a seam.
3. List the concrete test cases you will cover (happy path, errors, edge cases, sequences, interactions) and which existing tests already cover parts of it.
4. Stop and wait for explicit approval of the design + test plan.

## After approval
- Write failing tests first (or extend existing ones).
- Implement the minimum code to make the tests pass.
- Update documentation in the same change.

## After the changes
Complete a short summary including:
- What was added/changed and why.
- Mapping of each new/updated test to the code paths it exercises.
- Any remaining design debt or follow-up items.

# Architecture
A module is deep when a small interface hides a lot of behaviour. Depth is that leverage, not the number of lines. The interface includes ordering, errors, and configuration, not only the method names. When a module gets too large, it contains submodules. Those submodules stay inside. They are not a new surface for `main.cpp`.

`src/main.cpp` is the composition root. It constructs the ESP32 adapters, passes timing constants and `kMqttDeviceId`, and calls `begin` / `update`. It does not sequence the steps inside a module. Timing knobs and the default MQTT topic root stay in `main.cpp`. The topic layout must not invent `watering` itself.

The controller is seven modules:

- `Network` — Wi-Fi, NTP, MQTT, persisted config, and inbound fan-out. `update()` runs Wi-Fi, then NTP, then MQTT.
- `SerialConsole` — USB command lines in `update()`. The periodic snapshot is `updateStatus()`, after the modules have advanced. An immediate `status` command still prints inside `update()`.
- `ModuleBus` — four-slot enumeration and retained slot status. `update()` runs the host, then the slot publisher.
- `SensorModule`, `SolenoidModule`, `PumpModule` — one daughter type each. `update()` runs that type's poller, then its bridge. Each registers its MQTT handler in its constructor, so construct them before `Network::begin`.
- `Programming` — Arduino-as-ISP on slot 1. `begin()` quiesces the bus. While `active()`, `loop()` does not call the console, the bus, or the type modules.

Folders:

- `lib/Interfaces/` — hardware ports only (`IClock`, `IWifi`, `INtpAdapter`, `IMqttClient`, `IPreferenceStore`, `ISerialPort`, `IBytePort`, `IDigitalPin`, `ISpiMaster`, `I2cMaster`, and `ModuleProtocol.h`).
- `lib/Drivers/` — ESP32 adapters for those ports.
- `lib/Logic/` — one PlatformIO library. Modules live in `Network`, `Console`, `Bus`, `Sensor`, `Solenoid`, `Pump`, and `Programming`.
- `src/` — composition. No product logic that belongs inside a module.

A seam exists where two adapters implement one contract. The ESP32 driver and the desktop fake are the usual pair. `INetworkConfigStore` is a seam inside `Network` because `PreferenceNetworkConfigStore` and `FakeNetworkConfigStore` are both real adapters. Do not add an abstract interface so a sibling can be faked. One adapter is a hypothetical seam.

Sibling modules reach another module through a private accessor (`Network::mqtt()`, `ModuleBus::host()`, and the same pattern). Those accessors are not the public interface. `main.cpp` does not call them.

Follow DRY: do not duplicate knowledge or behaviour; factor shared logic into one well-named place. Each module does one job and can be understood on its own.

`platformio.ini` lists `lib_deps = Logic` on both environments. Logic sources live in subfolders. PlatformIO's library finder does not compile those sources unless the library is named. The `-I lib/Logic/...` flags only make the headers visible. Do not remove `lib_deps`.

# Coding Rules
Follow these rules for every change. All new code must be fully testable on the desktop by design.

1. Desktop-only testing
   - All tests run on the desktop (PlatformIO native environment). No hardware required.
   - Use Unity. Tests live under test/test_desktop/.
   - Run tests with: C:\Users\Nathan\.platformio\penv\Scripts\platformio.exe test -e native

2. Adapters at real seams
   - Inject hardware ports and any other contract that has two adapters. Prefer constructor injection.
   - Provide a controllable, observable fake for each of those ports.
   - Do not make every class replaceable. Submodules are tested through the module interface, and through the submodule tests that already cover their edges.

3. Test-Driven Development
   - Name the module and its interface, then write failing tests through that interface with the existing fakes, then implement the minimum.
   - Keep existing submodule tests. They cover edges the module tests do not replace. Do not delete them when a facade is added.
   - Add a new abstract interface only when a second adapter is required.
   - Cover happy paths, errors, edge cases, sequences, and interactions.

4. Naming & code style
   - Variables & functions: camelCase. Private members: leading underscore (_privateVar).
   - Classes/Types: PascalCase.
   - Every function/method has a clear docstring (purpose, params, return value).
   - Separate major sections with exactly: // ========== Section Name ==========
   - 1 blank line between functions, 2 blank lines between major sections.
   - Prefer clear descriptive names, keep lines reasonably short, consistent indentation and formatting.

5. Documentation
   - Maintain docs/ARCHITECTURE.md: the seven modules, their interfaces, seams, how `main.cpp` composes them, and desktop testing.
   - For every major module keep a short docs/ file covering purpose, public API, key classes, important states/sequences, configuration, and how it is tested with fakes.
   - Document each hardware port (meaning of methods, success/failure behaviour, what fakes must support) in docs/INTERFACES.md.
   - `INetworkConfigStore` is documented with Network. It is not a hardware port.
   - Documentation is high-level and practical (what & why). Update it in the same change when behaviour changes.
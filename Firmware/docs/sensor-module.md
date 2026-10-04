# Sensor module

[Home](home.md) · Reference: [Sensor](reference/sensor-module.md)

## Summary

`SensorModule` talks to daughter boards of type `0x0200`. A board reports up to 16 inputs. The module learns that count when the board is identified, then reads presence and a raw value for each input. Every stored reading is published, including a repeated value.

Wire indexes are 0-based. MQTT slot and sensor numbers are 1-based. The controller does not scale the value.

## Where it sits

`main.cpp` constructs it with `ModuleBus`, `Network`, the clock, and `kSensorPollIntervalMs` (60 seconds). The constructor registers the handler for `{id}/sensor/read` and `{id}/sensor/connected`, before `Network::begin()`. `loop()` calls `sensorModule.update()` after `moduleBus.update()` and before the solenoid module.

## What it creates

```text
main.cpp
  SensorModule
    SensorPoller         one query per pass; per-slot count, samples, and timer
    SensorMqttBridge     publishes stored readings and the connected list
```

`ModuleHost`, inside the bus, is the only I2C caller. The poller's exchange is separate from the one transaction inside `moduleBus.update()`.

## One update

```mermaid
flowchart TD
  upd["SensorModule.update()"] --> poll["SensorPoller: at most one exchange"]
  poll --> order["Immediate read, else a missing count, else the next cycle step"]
  order --> mem["Store the count or the sample"]
  mem --> bridge["SensorMqttBridge"]
  bridge --> pub{"A reading was stored, or the connected list changed?"}
  pub -->|yes| mqtt["Publish the retained reading, and the list when it changed"]
  pub -->|no| quiet["Leave the broker as it is"]
```

The callback for `{id}/sensor/read` only enqueues a slot and a sensor index. The poller sends `GET_SENSOR_READING` on a later pass. While the count is still unknown, that request waits and the count query runs instead. `{id}/sensor/connected` only asks for the connected-input list. It does not enqueue a reading.

## What is stored, and who reads it

Each slot keeps its own count, samples, revision, and poll timer. The bridge reads those and publishes `{id}/slot/N/sensor/M` when a reading was stored, and `{id}/slot/N/sensors` as the count plus the connected indexes. Unplug publishes retained `unavailable` once for that slot's inputs and for its list. Another slot keeps its own cache.

Serial status prints the slot line (`Online Sensor addr=0x10`). It does not print individual inputs.

## See also

- [Sensor reference](reference/sensor-module.md) — commands, poll order, publish rules, tests.
- [MQTT reference](reference/mqtt.md) — the sensor topics, and a payload to publish when testing.
- [Daughter contract](daughter/contract.md)
- [Module bus](module-bus.md)
- [Architecture](architecture.md)

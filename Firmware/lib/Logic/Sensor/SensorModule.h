#pragma once

#include <cstdint>

#include "IClock.h"
#include "SensorMqttBridge.h"
#include "SensorPoller.h"

class ModuleBus;
class Network;

/**
 * Sensor daughter behaviour: count, presence, readings, the immediate
 * read command, and retained publication. update() issues at most one
 * bus exchange and then publishes what that exchange stored.
 */
class SensorModule
{
public:
  /**
   * Creates a sensor module and registers its MQTT command handler.
   *
   * @param bus Module bus. Must outlive this object.
   * @param network MQTT client, topics, and inbound fan-out.
   * @param clock Monotonic clock.
   * @param pollIntervalMs Period for presence and readings. Zero selects
   *        the poller's default.
   */
  SensorModule(ModuleBus& bus, Network& network, IClock& clock,
               uint32_t pollIntervalMs);

  /**
   * Issues at most one sensor exchange, then publishes stored readings.
   *
   * @return Nothing.
   */
  void update();

private:
  SensorPoller _poller;
  SensorMqttBridge _bridge;
};

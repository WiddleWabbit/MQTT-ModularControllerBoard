#pragma once

#include <cstdint>

#include "IClock.h"
#include "PumpMqttBridge.h"
#include "PumpPoller.h"

class ModuleBus;
class Network;

/**
 * Pump daughter behaviour: state polls, on/off and reset commands,
 * the absence cutoff, and retained publication. update() issues at
 * most one bus exchange and then publishes what that exchange stored.
 */
class PumpModule
{
public:
  /**
   * Creates a pump module and registers its MQTT command handler.
   *
   * @param bus Module bus. Must outlive this object.
   * @param network MQTT client, topics, and inbound fan-out.
   * @param clock Monotonic clock.
   * @param pollIntervalMs Period between state polls. Zero selects
   *        the poller's default.
   * @param commandTimeoutMs Silence after which a pump that is on is
   *        commanded off. Zero selects the poller's default.
   */
  PumpModule(ModuleBus& bus, Network& network, IClock& clock,
             uint32_t pollIntervalMs, uint32_t commandTimeoutMs);

  /**
   * Issues at most one pump exchange, then publishes the stored state.
   *
   * @return Nothing.
   */
  void update();

private:
  PumpPoller _poller;
  PumpMqttBridge _bridge;
};

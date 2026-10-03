#pragma once

#include <cstdint>

#include "IClock.h"
#include "SolenoidMqttBridge.h"
#include "SolenoidPoller.h"

class ModuleBus;
class Network;

/**
 * Solenoid daughter behaviour: output count, desired-state commands,
 * the absence cutoff, and retained publication. update() issues at
 * most one bus exchange and then publishes what that exchange stored.
 */
class SolenoidModule
{
public:
  /**
   * Creates a solenoid module and registers its MQTT command handler.
   *
   * @param bus Module bus. Must outlive this object.
   * @param network MQTT client, topics, and inbound fan-out.
   * @param clock Monotonic clock.
   * @param pollIntervalMs Period between state polls. Zero selects
   *        the poller's default.
   * @param commandTimeoutMs Silence after which every output is
   *        commanded off. Zero selects the poller's default.
   */
  SolenoidModule(ModuleBus& bus, Network& network, IClock& clock,
                 uint32_t pollIntervalMs, uint32_t commandTimeoutMs);

  /**
   * Issues at most one solenoid exchange, then publishes stored states.
   *
   * @return Nothing.
   */
  void update();

private:
  SolenoidPoller _poller;
  SolenoidMqttBridge _bridge;
};

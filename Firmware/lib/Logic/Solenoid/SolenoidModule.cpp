#include "SolenoidModule.h"

#include "ModuleBus.h"
#include "Network.h"

// ========== Construction ==========

SolenoidModule::SolenoidModule(ModuleBus& bus, Network& network, IClock& clock,
                               uint32_t pollIntervalMs,
                               uint32_t commandTimeoutMs)
  : _poller(bus.host(), clock, pollIntervalMs, commandTimeoutMs),
    _bridge(_poller, network.mqtt(), network.topics())
{
  network.addMessageHandler(&SolenoidMqttBridge::onMqttMessage, &_bridge);
}


// ========== Public API ==========

/**
 * Issues at most one solenoid exchange, then publishes stored states.
 *
 * @return Nothing.
 */
void SolenoidModule::update()
{
  _poller.update();
  _bridge.update();
}

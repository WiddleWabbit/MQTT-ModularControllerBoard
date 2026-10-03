#include "SensorModule.h"

#include "ModuleBus.h"
#include "Network.h"

// ========== Construction ==========

SensorModule::SensorModule(ModuleBus& bus, Network& network, IClock& clock,
                           uint32_t pollIntervalMs)
  : _poller(bus.host(), clock, pollIntervalMs),
    _bridge(_poller, network.mqtt(), network.topics())
{
  network.addMessageHandler(&SensorMqttBridge::onMqttMessage, &_bridge);
}


// ========== Public API ==========

/**
 * Issues at most one sensor exchange, then publishes stored readings
 * and the connected-input list.
 *
 * @return Nothing.
 */
void SensorModule::update()
{
  _poller.update();
  _bridge.update();
}

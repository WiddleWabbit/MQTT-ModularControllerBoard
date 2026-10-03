#include "PumpModule.h"

#include "ModuleBus.h"
#include "Network.h"

// ========== Construction ==========

PumpModule::PumpModule(ModuleBus& bus, Network& network, IClock& clock,
                       uint32_t pollIntervalMs, uint32_t commandTimeoutMs)
  : _poller(bus.host(), clock, pollIntervalMs, commandTimeoutMs),
    _bridge(_poller, network.mqtt(), network.topics())
{
  network.addMessageHandler(&PumpMqttBridge::onMqttMessage, &_bridge);
}


// ========== Public API ==========

/**
 * Issues at most one pump exchange, then publishes the stored state.
 *
 * @return Nothing.
 */
void PumpModule::update()
{
  _poller.update();
  _bridge.update();
}

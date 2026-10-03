#include "ModuleBus.h"

#include "Network.h"

// ========== Construction ==========

ModuleBus::ModuleBus(I2cMaster& bus, IClock& clock, SlotPins slots[4],
                     const ModuleHostConfig& config, Network& network)
  : _host(bus, clock, slots, config),
    _publisher(_host, network.mqtt(), network.topics())
{
}


// ========== Public API ==========

/**
 * Configures slot pins and starts I2C.
 *
 * @return Nothing.
 */
void ModuleBus::begin()
{
  _host.begin();
}

/**
 * Advances the host, then publishes changed slot text.
 *
 * @return Nothing.
 */
void ModuleBus::update()
{
  _host.update();
  _publisher.update();
}

/**
 * Releases MOD and stops host traffic.
 *
 * @return Nothing.
 */
void ModuleBus::quiesce()
{
  _host.quiesce();
}

/**
 * Allows host traffic again.
 *
 * @return Nothing.
 */
void ModuleBus::resume()
{
  _host.resume();
}


// ========== Sibling access ==========

/**
 * Returns the host used by sibling modules.
 *
 * @return Module host.
 */
ModuleHost& ModuleBus::host()
{
  return _host;
}

#include "Programming.h"

#include "ModuleBus.h"

// ========== Construction ==========

Programming::Programming(IBytePort& port, ISpiMaster& spi, IDigitalPin& reset,
                         ModuleBus& modules, IClock& clock,
                         uint32_t idleTimeoutMs, uint32_t unplugTimeoutMs)
  : _programmer(port, spi, reset, clock),
    _session(_programmer, modules.host(), reset, port, clock, idleTimeoutMs,
             unplugTimeoutMs)
{
}


// ========== Public API ==========

/**
 * Quiesces the bus and drives reset high.
 *
 * @return Nothing.
 */
void Programming::begin()
{
  _session.begin();
}

/**
 * Services STK500 until the session ends.
 *
 * @return Nothing.
 */
void Programming::update()
{
  _session.update();
}

/**
 * Reports whether the session owns the bus and the USB byte stream.
 *
 * @return True until the session ends.
 */
bool Programming::active() const
{
  return _session.active();
}

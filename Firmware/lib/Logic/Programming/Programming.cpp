#include "Programming.h"

#include "ModuleBus.h"

// ========== Construction ==========

Programming::Programming(IBytePort& port, ISpiMaster& spi, IHalfDuplexUart& uart,
                         IDigitalPin& cs1, IDigitalPin& cs2, IDigitalPin& cs3,
                         IDigitalPin& cs4, const ProgrammingPins& pins,
                         ModuleBus& modules, IClock& clock,
                         uint32_t idleTimeoutMs, uint32_t unplugTimeoutMs)
  : _isp(port, spi, cs1, clock),
    _updi(port, uart, clock),
    _session(modules.host(), port, clock, idleTimeoutMs, unplugTimeoutMs),
    _pins(pins)
{
  _cs[0] = &cs1;
  _cs[1] = &cs2;
  _cs[2] = &cs3;
  _cs[3] = &cs4;
}


// ========== Public API ==========

/**
 * Selects the slot pin and starts ISP or UPDI. A slot outside 1..4
 * does not quiesce the bus. A second call while active does not
 * rebind the pin or the UART.
 *
 * @param slot Firmware slot, 1 through 4.
 * @param method ISP or UPDI.
 * @return Nothing.
 */
void Programming::begin(uint8_t slot, ProgrammingMethod method)
{
  if (_session.active() || slot < 1 || slot > 4)
  {
    return;
  }
  IDigitalPin& line = *_cs[slot - 1];
  const uint8_t gpio = _pins.csGpio[slot - 1];
  if (method == ProgrammingMethod::Isp)
  {
    _isp.start(line, gpio);
    _session.begin(_isp);
    return;
  }
  if (method == ProgrammingMethod::Updi)
  {
    _updi.start(line, gpio);
    _session.begin(_updi);
  }
}

/**
 * Services the active programmer until the session ends.
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

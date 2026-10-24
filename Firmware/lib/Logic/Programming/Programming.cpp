#include "Programming.h"

#include "ModuleBus.h"

// ========== Construction ==========

Programming::Programming(IBytePort& port, ISpiMaster& spi, IHalfDuplexUart& uart,
                         IDigitalPin& cs1, IDigitalPin& cs2, IDigitalPin& cs3,
                         IDigitalPin& cs4, IDigitalPin& mod1, IDigitalPin& mod2,
                         IDigitalPin& mod3, IDigitalPin& mod4,
                         const ProgrammingPins& pins, ModuleBus& modules,
                         IClock& clock, uint32_t idleTimeoutMs,
                         uint32_t unplugTimeoutMs)
  : _isp(port, spi, cs1, clock),
    _updi(port, uart, clock),
    _session(modules.host(), port, clock, idleTimeoutMs, unplugTimeoutMs),
    _pins(pins)
{
  _cs[0] = &cs1;
  _cs[1] = &cs2;
  _cs[2] = &cs3;
  _cs[3] = &cs4;
  _mod[0] = &mod1;
  _mod[1] = &mod2;
  _mod[2] = &mod3;
  _mod[3] = &mod4;
}


// ========== Public API ==========

/**
 * Selects the slot pin and starts ISP or UPDI. An in-progress link
 * check is released first. A slot outside 1..4 does not quiesce the
 * bus. A second call while active does not rebind the pin or the UART.
 *
 * @param slot Firmware slot, 1 through 4.
 * @param method ISP or UPDI.
 * @return Nothing.
 */
void Programming::begin(uint8_t slot, ProgrammingMethod method)
{
  if (_checkActive)
  {
    _updi.shutdown();
    _checkActive = false;
  }
  if (_session.active() || slot < 1 || slot > 4)
  {
    return;
  }
  if (method == ProgrammingMethod::Isp)
  {
    _isp.start(*_cs[slot - 1], _pins.csGpio[slot - 1]);
    _session.begin(_isp);
    return;
  }
  if (method == ProgrammingMethod::Updi)
  {
    // Quiesce releases every MOD line. Start after that so the
    // pull-up on the selected line is not cleared.
    _session.begin(_updi);
    _updi.start(*_mod[slot - 1], _pins.modGpio[slot - 1]);
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

/**
 * Binds the slot MOD pin and samples it. A session or a check already
 * in progress is left as it is.
 *
 * @param slot Firmware slot, 1 through 4.
 * @return True when this call started the measurement.
 */
bool Programming::startUpdiCheck(uint8_t slot)
{
  if (_checkActive || _session.active() || slot < 1 || slot > 4)
  {
    return false;
  }
  _updi.beginLinkCheck(*_mod[slot - 1], _pins.modGpio[slot - 1]);
  _checkActive = true;
  return true;
}

/**
 * Advances one phase of the check's double break. Clears the running
 * flag on the call that finishes the second low.
 *
 * @param result Receives the measurement when this call finishes it.
 * @return True once, after the measurement is finished and the pin
 *         is restored.
 */
bool Programming::pollUpdiCheck(UpdiLinkCheck& result)
{
  if (!_checkActive)
  {
    return false;
  }
  if (!_updi.pollLinkCheck(result))
  {
    return false;
  }
  _checkActive = false;
  return true;
}

/**
 * Reports the link check that startUpdiCheck started.
 *
 * @return True until the measurement is finished or cancelled.
 */
bool Programming::updiCheckActive() const
{
  return _checkActive;
}

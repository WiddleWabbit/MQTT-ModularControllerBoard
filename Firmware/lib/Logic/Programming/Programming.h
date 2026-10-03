#pragma once

#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IDigitalPin.h"
#include "ISpiMaster.h"
#include "IspProgrammer.h"
#include "ProgrammingSession.h"

class ModuleBus;

/**
 * Arduino-as-ISP session for firmware slot 1. begin() quiesces the
 * module bus and holds reset idle-high. update() speaks STK500 until
 * the session's idle or unplug rule ends it.
 */
class Programming
{
public:
  /**
   * Creates a session around the slot-1 reset pin. The bus, port, SPI
   * master, and reset pin must outlive this object.
   *
   * @param port USB byte stream shared with the console.
   * @param spi SPI master for the target.
   * @param reset Slot-1 reset pin.
   * @param modules Module bus quiesced for the session.
   * @param clock Monotonic clock.
   * @param idleTimeoutMs Silence that ends the session.
   * @param unplugTimeoutMs Absence, after the port has been seen, that
   *        ends the session.
   */
  Programming(IBytePort& port, ISpiMaster& spi, IDigitalPin& reset,
              ModuleBus& modules, IClock& clock, uint32_t idleTimeoutMs,
              uint32_t unplugTimeoutMs);

  /**
   * Quiesces the bus and drives reset high. No-op when already active.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Services STK500. Ends after a confirmed unplug or idle timeout.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Reports whether the session owns the bus and the USB byte stream.
   *
   * @return True until the session ends.
   */
  bool active() const;

private:
  IspProgrammer _programmer;
  ProgrammingSession _session;
};

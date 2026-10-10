#pragma once

#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IDigitalPin.h"
#include "IHalfDuplexUart.h"
#include "ISpiMaster.h"
#include "IspProgrammer.h"
#include "ProgrammingSession.h"
#include "ProgrammingTarget.h"
#include "UpdiProgrammer.h"

class ModuleBus;

/**
 * Programming session for one firmware slot. begin() selects ISP or
 * UPDI, quiesces the module bus, and hands the USB byte stream to that
 * programmer. update() runs until the session's idle or unplug rule
 * ends it.
 */
class Programming
{
public:
  /**
   * Creates both programmers. The bus, port, SPI master, UART, pins,
   * and pin objects must outlive this object.
   *
   * @param port USB byte stream shared with the console.
   * @param spi SPI master for ISP. Unused by UPDI.
   * @param uart Single-wire UART for UPDI. Unused by ISP.
   * @param cs1 Slot 1 chip-select.
   * @param cs2 Slot 2 chip-select.
   * @param cs3 Slot 3 chip-select.
   * @param cs4 Slot 4 chip-select.
   * @param pins GPIO numbers printed by the console and used as the
   *        UPDI wire number. ISP uses the pin objects, not these numbers.
   * @param modules Module bus quiesced for the session.
   * @param clock Monotonic clock.
   * @param idleTimeoutMs Silence that ends the session.
   * @param unplugTimeoutMs Absence, after the port has been seen, that
   *        ends the session.
   */
  Programming(IBytePort& port, ISpiMaster& spi, IHalfDuplexUart& uart,
              IDigitalPin& cs1, IDigitalPin& cs2, IDigitalPin& cs3,
              IDigitalPin& cs4, const ProgrammingPins& pins,
              ModuleBus& modules, IClock& clock, uint32_t idleTimeoutMs,
              uint32_t unplugTimeoutMs);

  /**
   * Starts a session on slot 1..4. No-op when a session is active or
   * the slot is outside that range. ISP drives the slot CS pin high.
   * UPDI leaves it idle and does not start SPI.
   *
   * @param slot Firmware slot, 1 through 4.
   * @param method ISP or UPDI.
   * @return Nothing.
   */
  void begin(uint8_t slot, ProgrammingMethod method);

  /**
   * Services the active programmer. Ends after a confirmed unplug or
   * idle timeout.
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
  IspProgrammer _isp;
  UpdiProgrammer _updi;
  ProgrammingSession _session;
  IDigitalPin* _cs[4];
  const ProgrammingPins& _pins;
};

#pragma once

#include <cstdint>

#include "IDigitalPin.h"

/**
 * One programming protocol on the USB byte stream. ISP and UPDI both
 * implement this. The session calls it and does not know which wire
 * is in use.
 */
class IProgrammer
{
public:
  virtual ~IProgrammer() = default;

  /**
   * Binds the slot line for this session. ISP drives it high. UPDI
   * leaves it idle. Neither call starts a transfer.
   *
   * @param line Pin used as RESET or as the UPDI wire.
   * @param gpio GPIO number of that pin. ISP ignores it.
   * @return Nothing.
   */
  virtual void start(IDigitalPin& line, uint8_t gpio) = 0;

  /**
   * Consumes queued host bytes and continues any timed step.
   *
   * @return Nothing.
   */
  virtual void update() = 0;

  /**
   * Releases the wire and returns the slot line to an input pull-up.
   *
   * @return Nothing.
   */
  virtual void shutdown() = 0;

  /**
   * Counts bytes accepted from the host port.
   *
   * @return Monotonic count of accepted bytes.
   */
  virtual uint32_t activityCount() const = 0;
};

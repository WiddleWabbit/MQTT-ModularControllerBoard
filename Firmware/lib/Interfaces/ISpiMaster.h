#pragma once

#include <cstdint>

/**
 * SPI master used to clock an AVR while it is held in reset.
 * Mode 0, MSB first. begin() leaves SCK idle low.
 */
class ISpiMaster
{
public:
  virtual ~ISpiMaster() = default;

  /**
   * Starts the bus at clockHz. SCK idles low.
   *
   * @param clockHz SPI clock in hertz.
   * @return Nothing.
   */
  virtual void begin(uint32_t clockHz) = 0;

  /**
   * Releases the bus.
   *
   * @return Nothing.
   */
  virtual void end() = 0;

  /**
   * Clocks one byte.
   *
   * @param tx Byte shifted out on MOSI.
   * @return Byte shifted in on MISO.
   */
  virtual uint8_t transfer(uint8_t tx) = 0;
};

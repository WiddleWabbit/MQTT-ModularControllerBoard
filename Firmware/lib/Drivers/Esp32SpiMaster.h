#pragma once

#include <cstdint>

#include "ISpiMaster.h"

/**
 * SPI master on the board's shared MOSI, MISO, and SCK pins.
 * Mode 0, MSB first. SCK idles low for the whole session.
 */
class Esp32SpiMaster : public ISpiMaster
{
public:
  /**
   * Creates a master that will claim these GPIOs in begin().
   *
   * @param sck SCK GPIO.
   * @param miso MISO GPIO.
   * @param mosi MOSI GPIO.
   */
  Esp32SpiMaster(int8_t sck, int8_t miso, int8_t mosi);

  /**
   * Starts the bus at clockHz and holds one transaction open.
   *
   * @param clockHz SPI clock in hertz.
   * @return Nothing.
   */
  void begin(uint32_t clockHz) override;

  /**
   * Ends the transaction and releases the bus.
   *
   * @return Nothing.
   */
  void end() override;

  /**
   * Clocks one byte.
   *
   * @param tx Byte shifted out on MOSI.
   * @return Byte shifted in on MISO.
   */
  uint8_t transfer(uint8_t tx) override;

private:
  int8_t _sck;
  int8_t _miso;
  int8_t _mosi;
  bool _active = false;
};

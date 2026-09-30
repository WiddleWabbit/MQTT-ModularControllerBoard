#include "Esp32SpiMaster.h"

#include <SPI.h>

// ========== Construction ==========

Esp32SpiMaster::Esp32SpiMaster(int8_t sck, int8_t miso, int8_t mosi)
  : _sck(sck), _miso(miso), _mosi(mosi)
{
}


// ========== Public API ==========

/**
 * Starts the bus at clockHz and holds one transaction open.
 * SCK idles low because the mode is 0. No hardware chip-select.
 *
 * @param clockHz SPI clock in hertz.
 * @return Nothing.
 */
void Esp32SpiMaster::begin(uint32_t clockHz)
{
  if (_active)
  {
    SPI.endTransaction();
    SPI.end();
  }
  SPI.begin(_sck, _miso, _mosi, -1);
  SPI.beginTransaction(SPISettings(clockHz, SPI_MSBFIRST, SPI_MODE0));
  _active = true;
}

/**
 * Ends the transaction and releases the bus.
 *
 * @return Nothing.
 */
void Esp32SpiMaster::end()
{
  if (!_active)
  {
    return;
  }
  SPI.endTransaction();
  SPI.end();
  _active = false;
}

/**
 * Clocks one byte.
 *
 * @param tx Byte shifted out on MOSI.
 * @return Byte shifted in on MISO.
 */
uint8_t Esp32SpiMaster::transfer(uint8_t tx)
{
  return SPI.transfer(tx);
}

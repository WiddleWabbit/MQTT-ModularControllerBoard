#include "Esp32I2cMaster.h"

#include <Arduino.h>

// ========== Construction ==========

Esp32I2cMaster::Esp32I2cMaster(TwoWire& wire, uint8_t sda, uint8_t scl)
  : _wire(wire),
    _sda(sda),
    _scl(scl)
{
}


// ========== Public API ==========

/**
 * Initializes the bus pins and applies clock and timeout.
 *
 * @return Nothing.
 */
void Esp32I2cMaster::begin()
{
  _wire.begin(_sda, _scl);
  _wire.setClock(_clockHz);
  _wire.setTimeOut(_timeoutMs);
}

/**
 * Sets the I2C bit clock.
 *
 * @param hz Bit clock in hertz.
 * @return Nothing.
 */
void Esp32I2cMaster::setClockHz(uint32_t hz)
{
  _clockHz = hz;
  _wire.setClock(_clockHz);
}

/**
 * Sets the transaction timeout.
 *
 * @param ms Timeout in milliseconds.
 * @return Nothing.
 */
void Esp32I2cMaster::setTimeoutMs(uint32_t ms)
{
  _timeoutMs = ms;
  _wire.setTimeOut(_timeoutMs);
}

/**
 * Writes bytes and issues STOP.
 *
 * @param address 7-bit slave address.
 * @param data Bytes to write.
 * @param length Number of bytes.
 * @return Transaction status.
 */
I2cTxnStatus Esp32I2cMaster::write(uint8_t address, const uint8_t* data,
                                   size_t length)
{
  _wire.beginTransmission(address);
  if (length > 0 && data != nullptr)
  {
    _wire.write(data, length);
  }
  return _mapEndTransmission(_wire.endTransmission(true));
}

/**
 * Reads bytes from a 7-bit address.
 *
 * @param address 7-bit slave address.
 * @param buffer Destination buffer.
 * @param length Number of bytes to read.
 * @return Transaction status.
 */
I2cTxnStatus Esp32I2cMaster::read(uint8_t address, uint8_t* buffer,
                                  size_t length)
{
  const size_t got = _wire.requestFrom(static_cast<int>(address),
                                       static_cast<int>(length));
  if (got != length)
  {
    return I2cTxnStatus::Nack;
  }
  for (size_t i = 0; i < length; ++i)
  {
    buffer[i] = static_cast<uint8_t>(_wire.read());
  }
  return I2cTxnStatus::Ok;
}

/**
 * Writes then reads with a repeated start. Skips the read on write NACK.
 *
 * @param address 7-bit slave address.
 * @param tx Bytes to write.
 * @param txLen Write length.
 * @param rx Read destination.
 * @param rxLen Read length.
 * @return Transaction status.
 */
I2cTxnStatus Esp32I2cMaster::writeRead(uint8_t address, const uint8_t* tx,
                                       size_t txLen, uint8_t* rx,
                                       size_t rxLen)
{
  _wire.beginTransmission(address);
  if (txLen > 0 && tx != nullptr)
  {
    _wire.write(tx, txLen);
  }
  const I2cTxnStatus writeStatus =
      _mapEndTransmission(_wire.endTransmission(false));
  if (writeStatus != I2cTxnStatus::Ok)
  {
    return writeStatus;
  }
  return read(address, rx, rxLen);
}

/**
 * Best-effort bus clear by bit-banging SCL then re-init.
 *
 * @return True when SDA is high after the attempt.
 */
bool Esp32I2cMaster::recover()
{
  _wire.end();
  pinMode(_sda, INPUT_PULLUP);
  pinMode(_scl, OUTPUT);
  for (uint8_t i = 0; i < 9; ++i)
  {
    digitalWrite(_scl, HIGH);
    delayMicroseconds(5);
    if (digitalRead(_sda) != LOW)
    {
      break;
    }
    digitalWrite(_scl, LOW);
    delayMicroseconds(5);
  }
  pinMode(_sda, OUTPUT);
  digitalWrite(_sda, LOW);
  digitalWrite(_scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(_sda, HIGH);
  delayMicroseconds(5);
  begin();
  pinMode(_sda, INPUT_PULLUP);
  return digitalRead(_sda) != LOW;
}


// ========== Private Helpers ==========

/**
 * Maps a TwoWire endTransmission code to I2cTxnStatus.
 *
 * @param code Wire endTransmission result.
 * @return Transaction status.
 */
I2cTxnStatus Esp32I2cMaster::_mapEndTransmission(uint8_t code)
{
  switch (code)
  {
    case 0:
      return I2cTxnStatus::Ok;
    case 2:
    case 3:
      return I2cTxnStatus::Nack;
    case 5:
      return I2cTxnStatus::Timeout;
    default:
      return I2cTxnStatus::BusError;
  }
}

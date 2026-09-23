#pragma once

#include <cstddef>
#include <cstdint>

#include <Wire.h>

#include "I2cMaster.h"

/**
 * Adapts Arduino TwoWire to I2cMaster with STOP writes and bus recover.
 */
class Esp32I2cMaster : public I2cMaster
{
public:
  /**
   * Creates an I2C master on the given TwoWire instance and pins.
   *
   * @param wire TwoWire bus.
   * @param sda SDA GPIO.
   * @param scl SCL GPIO.
   */
  Esp32I2cMaster(TwoWire& wire, uint8_t sda, uint8_t scl);

  /**
   * Initializes the bus pins and applies clock and timeout.
   *
   * @return Nothing.
   */
  void begin() override;

  /**
   * Sets the I2C bit clock.
   *
   * @param hz Bit clock in hertz.
   * @return Nothing.
   */
  void setClockHz(uint32_t hz) override;

  /**
   * Sets the transaction timeout.
   *
   * @param ms Timeout in milliseconds.
   * @return Nothing.
   */
  void setTimeoutMs(uint32_t ms) override;

  /**
   * Writes bytes and issues STOP.
   *
   * @param address 7-bit slave address.
   * @param data Bytes to write.
   * @param length Number of bytes.
   * @return Transaction status.
   */
  I2cTxnStatus write(uint8_t address, const uint8_t* data,
                     size_t length) override;

  /**
   * Reads bytes from a 7-bit address.
   *
   * @param address 7-bit slave address.
   * @param buffer Destination buffer.
   * @param length Number of bytes to read.
   * @return Transaction status.
   */
  I2cTxnStatus read(uint8_t address, uint8_t* buffer, size_t length) override;

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
  I2cTxnStatus writeRead(uint8_t address, const uint8_t* tx, size_t txLen,
                         uint8_t* rx, size_t rxLen) override;

  /**
   * Best-effort bus clear by bit-banging SCL then re-init.
   *
   * @return True when SDA is high after the attempt.
   */
  bool recover() override;

private:
  TwoWire& _wire;
  uint8_t _sda;
  uint8_t _scl;
  uint32_t _clockHz = 100000;
  uint32_t _timeoutMs = 50;

  /**
   * Maps a TwoWire endTransmission code to I2cTxnStatus.
   *
   * @param code Wire endTransmission result.
   * @return Transaction status.
   */
  static I2cTxnStatus _mapEndTransmission(uint8_t code);
};

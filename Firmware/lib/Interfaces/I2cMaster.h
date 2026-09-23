#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Result of one I2C master transaction.
 */
enum class I2cTxnStatus : uint8_t
{
  Ok,
  Nack,
  Timeout,
  BusError
};

/**
 * Abstracts a 7-bit I2C master with bounded write, read, and recover.
 */
class I2cMaster
{
public:
  virtual ~I2cMaster() = default;

  /**
   * Initializes the bus pins and applies the last clock and timeout.
   *
   * @return Nothing.
   */
  virtual void begin() = 0;

  /**
   * Sets the I2C bit clock.
   *
   * @param hz Bit clock in hertz.
   * @return Nothing.
   */
  virtual void setClockHz(uint32_t hz) = 0;

  /**
   * Sets the transaction timeout.
   *
   * @param ms Timeout in milliseconds.
   * @return Nothing.
   */
  virtual void setTimeoutMs(uint32_t ms) = 0;

  /**
   * Writes bytes to a 7-bit address and issues STOP.
   *
   * @param address 7-bit slave address.
   * @param data Bytes to write.
   * @param length Number of bytes in data.
   * @return Transaction status.
   */
  virtual I2cTxnStatus write(uint8_t address, const uint8_t* data,
                             size_t length) = 0;

  /**
   * Reads bytes from a 7-bit address.
   *
   * @param address 7-bit slave address.
   * @param buffer Destination buffer.
   * @param length Number of bytes to read.
   * @return Transaction status.
   */
  virtual I2cTxnStatus read(uint8_t address, uint8_t* buffer,
                            size_t length) = 0;

  /**
   * Writes then reads with a repeated start. If the write phase NACKs,
   * times out, or bus-errors, returns that status without issuing the
   * read phase and leaves rx untouched.
   *
   * @param address 7-bit slave address.
   * @param tx Bytes to write.
   * @param txLen Number of bytes in tx.
   * @param rx Destination buffer for the read phase.
   * @param rxLen Number of bytes to read.
   * @return Transaction status.
   */
  virtual I2cTxnStatus writeRead(uint8_t address, const uint8_t* tx,
                                 size_t txLen, uint8_t* rx,
                                 size_t rxLen) = 0;

  /**
   * Best-effort bus clear: up to nine SCL clocks while SDA is observed,
   * then STOP, then re-init the peripheral.
   *
   * @return True when SDA is high after the attempt.
   */
  virtual bool recover() = 0;
};

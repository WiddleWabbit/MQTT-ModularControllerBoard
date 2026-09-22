#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "FakeModuleDevice.h"
#include "I2cMaster.h"

struct FakeI2cOp
{
  enum class Type : uint8_t
  {
    Write,
    Read,
    WriteRead,
    Recover
  };

  Type type;
  uint8_t address;
  std::vector<uint8_t> tx;
  size_t rxLen;
  bool issuedStop;
  I2cTxnStatus result;
};

class FakeI2cMaster : public I2cMaster
{
public:
  std::vector<FakeModuleDevice*> devices;
  std::vector<FakeI2cOp> ops;
  bool collision = false;
  bool stuckSda = false;
  bool stuckSdaClearsOnRecover = true;
  int beginCallCount = 0;
  int recoverCallCount = 0;
  uint32_t clockHz = 0;
  uint32_t timeoutMs = 0;

  /**
   * Records begin.
   *
   * @return Nothing.
   */
  void begin() override
  {
    beginCallCount++;
  }

  /**
   * Records the configured bit clock.
   *
   * @param hz Bit clock in hertz.
   * @return Nothing.
   */
  void setClockHz(uint32_t hz) override
  {
    clockHz = hz;
  }

  /**
   * Records the configured timeout.
   *
   * @param ms Timeout in milliseconds.
   * @return Nothing.
   */
  void setTimeoutMs(uint32_t ms) override
  {
    timeoutMs = ms;
  }

  /**
   * Writes with STOP to attached devices.
   *
   * @param address 7-bit address.
   * @param data Bytes to write.
   * @param length Byte count.
   * @return Transaction status.
   */
  I2cTxnStatus write(uint8_t address, const uint8_t* data,
                     size_t length) override
  {
    return _dispatchWrite(address, data, length, true);
  }

  /**
   * Unsupported standalone read; records and NACKs.
   *
   * @param address 7-bit address.
   * @param buffer Destination.
   * @param length Byte count.
   * @return Nack.
   */
  I2cTxnStatus read(uint8_t address, uint8_t* buffer, size_t length) override
  {
    (void)buffer;
    FakeI2cOp op{};
    op.type = FakeI2cOp::Type::Read;
    op.address = address;
    op.rxLen = length;
    op.issuedStop = true;
    op.result = I2cTxnStatus::Nack;
    ops.push_back(op);
    return I2cTxnStatus::Nack;
  }

  /**
   * Repeated-start write then read. Skips the read phase on write NACK.
   *
   * @param address 7-bit address.
   * @param tx Bytes to write.
   * @param txLen Write length.
   * @param rx Read destination.
   * @param rxLen Read length.
   * @return Transaction status.
   */
  I2cTxnStatus writeRead(uint8_t address, const uint8_t* tx, size_t txLen,
                         uint8_t* rx, size_t rxLen) override
  {
    FakeI2cOp op{};
    op.type = FakeI2cOp::Type::WriteRead;
    op.address = address;
    if (tx != nullptr && txLen > 0)
    {
      op.tx.assign(tx, tx + txLen);
    }
    op.rxLen = rxLen;
    op.issuedStop = false;

    if (stuckSda)
    {
      op.result = I2cTxnStatus::Timeout;
      ops.push_back(op);
      return I2cTxnStatus::Timeout;
    }

    std::vector<FakeModuleDevice*> acks;
    for (FakeModuleDevice* device : devices)
    {
      if (device != nullptr && device->acks(address))
      {
        acks.push_back(device);
      }
    }
    if (acks.size() > 1)
    {
      collision = true;
      op.result = I2cTxnStatus::BusError;
      ops.push_back(op);
      return I2cTxnStatus::BusError;
    }
    if (acks.empty())
    {
      op.result = I2cTxnStatus::Nack;
      ops.push_back(op);
      return I2cTxnStatus::Nack;
    }

    const I2cTxnStatus result =
        acks[0]->onWriteRead(address, tx, txLen, rx, rxLen);
    op.result = result;
    ops.push_back(op);
    return result;
  }

  /**
   * Clears stuck-SDA when the harness allows it.
   *
   * @return True when SDA is high after the attempt.
   */
  bool recover() override
  {
    recoverCallCount++;
    FakeI2cOp op{};
    op.type = FakeI2cOp::Type::Recover;
    op.issuedStop = true;
    op.result = I2cTxnStatus::Ok;
    ops.push_back(op);
    if (stuckSda && stuckSdaClearsOnRecover)
    {
      stuckSda = false;
    }
    return !stuckSda;
  }

  /**
   * Attaches a simulated module.
   *
   * @param device Module device.
   * @return Nothing.
   */
  void attach(FakeModuleDevice& device)
  {
    devices.push_back(&device);
  }

  /**
   * Counts recorded protocol transactions excluding recover.
   *
   * @return Number of write/read/writeRead ops.
   */
  size_t protocolOpCount() const
  {
    size_t count = 0;
    for (const FakeI2cOp& op : ops)
    {
      if (op.type != FakeI2cOp::Type::Recover)
      {
        count++;
      }
    }
    return count;
  }

private:
  /**
   * Dispatches a write to attached devices.
   *
   * @param address 7-bit address.
   * @param data Bytes to write.
   * @param length Byte count.
   * @param issuedStop True when STOP is issued.
   * @return Transaction status.
   */
  I2cTxnStatus _dispatchWrite(uint8_t address, const uint8_t* data,
                              size_t length, bool issuedStop)
  {
    FakeI2cOp op{};
    op.type = FakeI2cOp::Type::Write;
    op.address = address;
    if (data != nullptr && length > 0)
    {
      op.tx.assign(data, data + length);
    }
    op.rxLen = 0;
    op.issuedStop = issuedStop;

    if (stuckSda)
    {
      op.result = I2cTxnStatus::Timeout;
      ops.push_back(op);
      return I2cTxnStatus::Timeout;
    }

    std::vector<FakeModuleDevice*> acks;
    for (FakeModuleDevice* device : devices)
    {
      if (device != nullptr && device->acks(address))
      {
        acks.push_back(device);
      }
    }
    if (acks.size() > 1)
    {
      collision = true;
      op.result = I2cTxnStatus::BusError;
      ops.push_back(op);
      return I2cTxnStatus::BusError;
    }
    if (acks.empty())
    {
      op.result = I2cTxnStatus::Nack;
      ops.push_back(op);
      return I2cTxnStatus::Nack;
    }

    const I2cTxnStatus result =
        acks[0]->onWrite(address, data, length, issuedStop);
    op.result = result;
    ops.push_back(op);
    return result;
  }
};

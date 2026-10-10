#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "IHalfDuplexUart.h"

/**
 * Byte-level half-duplex UART. Each written byte is echoed before any
 * scripted target response. Tests queue only the target bytes.
 */
class FakeHalfDuplexUart : public IHalfDuplexUart
{
public:
  /**
   * Records the GPIO and baud and marks the UART attached.
   *
   * @param pin GPIO number.
   * @param rate Bit rate.
   * @return Nothing.
   */
  void attach(uint8_t pin, uint32_t rate) override
  {
    gpio = pin;
    baud = rate;
    attached = true;
    attachCount++;
  }

  /**
   * Drops the echo queue and marks the UART detached.
   *
   * @return Nothing.
   */
  void detach() override
  {
    attached = false;
    detachCount++;
    _echo.clear();
  }

  /**
   * Reports echoed bytes plus scripted responses.
   *
   * @return Number of bytes read() can return.
   */
  int available() const override
  {
    return static_cast<int>(_echo.size() + _replies.size());
  }

  /**
   * Returns the next echo, else the next scripted response, else -1.
   *
   * @return Next byte, or -1.
   */
  int read() override
  {
    if (!_echo.empty())
    {
      const int value = _echo.front();
      _echo.pop_front();
      return value;
    }
    if (!_replies.empty())
    {
      const int value = _replies.front();
      _replies.pop_front();
      return value;
    }
    return -1;
  }

  /**
   * Records transmitted bytes and queues an echo for each one.
   *
   * @param data Bytes to write.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void write(const uint8_t* data, size_t length) override
  {
    if (data == nullptr)
    {
      return;
    }
    for (size_t index = 0; index < length; ++index)
    {
      tx.push_back(data[index]);
      _echo.push_back(data[index]);
    }
  }

  /**
   * Queues one target response. Echoes are not queued here.
   *
   * @param value Response byte.
   * @return Nothing.
   */
  void reply(uint8_t value)
  {
    _replies.push_back(value);
  }

  /**
   * Queues a block of target responses.
   *
   * @param data Response bytes.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void reply(const uint8_t* data, size_t length)
  {
    if (data == nullptr)
    {
      return;
    }
    for (size_t index = 0; index < length; ++index)
    {
      _replies.push_back(data[index]);
    }
  }

  std::vector<uint8_t> tx;
  uint8_t gpio = 0;
  uint32_t baud = 0;
  bool attached = false;
  int attachCount = 0;
  int detachCount = 0;

private:
  std::deque<uint8_t> _echo;
  std::deque<uint8_t> _replies;
};

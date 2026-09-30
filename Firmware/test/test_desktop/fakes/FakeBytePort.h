#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "IBytePort.h"

/**
 * In-memory byte port for programming-protocol tests.
 */
class FakeBytePort : public IBytePort
{
public:
  bool isPlugged() const override
  {
    return plugged;
  }

  size_t available() const override
  {
    return input.size();
  }

  int read() override
  {
    if (input.empty())
    {
      return -1;
    }
    const int value = input.front();
    input.pop_front();
    return value;
  }

  void write(const uint8_t* data, size_t length) override
  {
    if (data == nullptr)
    {
      return;
    }
    for (size_t i = 0; i < length; ++i)
    {
      output.push_back(data[i]);
    }
  }

  /**
   * Queues bytes for the next read.
   *
   * @param data Bytes to present.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void feed(const uint8_t* data, size_t length)
  {
    if (data == nullptr)
    {
      return;
    }
    for (size_t i = 0; i < length; ++i)
    {
      input.push_back(data[i]);
    }
  }

  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  bool plugged = true;
};

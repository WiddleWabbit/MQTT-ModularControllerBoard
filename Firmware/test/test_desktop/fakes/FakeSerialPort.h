#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ISerialPort.h"

class FakeSerialPort : public ISerialPort
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

  void writeLine(const char* line) override
  {
    output.emplace_back(line == nullptr ? "" : line);
  }

  void feed(const std::string& text)
  {
    for (const char value : text)
    {
      input.push_back(static_cast<unsigned char>(value));
    }
  }

  std::deque<int> input;
  std::vector<std::string> output;
  bool plugged = true;
};

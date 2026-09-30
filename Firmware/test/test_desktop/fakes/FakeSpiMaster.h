#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ISpiMaster.h"

/**
 * Records SPI transfers and returns a scripted MISO stream.
 */
class FakeSpiMaster : public ISpiMaster
{
public:
  /**
   * Records the clock and optional event name "spi-begin".
   *
   * @param clockHz Requested clock.
   * @return Nothing.
   */
  void begin(uint32_t clockHz) override
  {
    begun = true;
    this->clockHz = clockHz;
    beginCount++;
    if (events != nullptr)
    {
      events->push_back("spi-begin");
    }
  }

  /**
   * Records that the bus was released.
   *
   * @return Nothing.
   */
  void end() override
  {
    begun = false;
    endCount++;
  }

  /**
   * Records tx and returns the next scripted rx byte, or 0.
   *
   * @param tx Byte shifted out.
   * @return Scripted response byte.
   */
  uint8_t transfer(uint8_t tx) override
  {
    sent.push_back(tx);
    if (scriptIndex < script.size())
    {
      return script[scriptIndex++];
    }
    return 0;
  }

  bool begun = false;
  uint32_t clockHz = 0;
  int beginCount = 0;
  int endCount = 0;
  std::vector<uint8_t> sent;
  std::vector<uint8_t> script;
  size_t scriptIndex = 0;
  std::vector<std::string>* events = nullptr;
};

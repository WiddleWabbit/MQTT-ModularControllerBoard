#pragma once

#include "INetworkConfigStore.h"

class FakeNetworkConfigStore : public INetworkConfigStore
{
public:
  bool load(NetworkConfig& config) override
  {
    loadCallCount++;
    if (!loadResult)
    {
      return false;
    }
    config = stored;
    return true;
  }

  bool save(const NetworkConfig& config) override
  {
    saveCallCount++;
    if (!saveResult)
    {
      return false;
    }
    stored = config;
    return true;
  }

  NetworkConfig stored{};
  bool loadResult = false;
  bool saveResult = true;
  int loadCallCount = 0;
  int saveCallCount = 0;
};

#pragma once

#include "INtpClient.h"

class FakeNtpClient : public INtpClient {
public:
  void requestSync(const char* serverName) override
  {
    ++requestCount;
    lastServer = serverName;
  }

  void update() override
  {
    ++updateCount;
  }

  bool isSynchronized() const override
  {
    return synchronized;
  }

  void reset()
  {
    requestCount = 0;
    updateCount = 0;
    lastServer = "";
    synchronized = false;
  }

  unsigned int requestCount = 0;
  unsigned int updateCount = 0;
  const char* lastServer = "";
  bool synchronized = false;
};

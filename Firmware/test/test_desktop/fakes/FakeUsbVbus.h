#pragma once

#include "IUsbVbus.h"

class FakeUsbVbus : public IUsbVbus
{
public:
  bool isPresent() const override
  {
    return present;
  }

  bool present = true;
};

#pragma once

#include "ISystemControl.h"

class FakeSystemControl : public ISystemControl {
public:
  /**
   * Records a simulated restart request.
   *
   * @return Nothing.
   */
  void restart() override
  {
    restartCallCount++;
  }

  unsigned int restartCallCount = 0;
};

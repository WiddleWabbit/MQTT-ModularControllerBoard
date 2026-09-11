#pragma once

#include "ISystemControl.h"

class Esp32SystemControl : public ISystemControl {
public:
  /**
   * Requests an ESP32 restart through the Arduino runtime.
   *
   * @return Nothing.
   */
  void restart() override;
};

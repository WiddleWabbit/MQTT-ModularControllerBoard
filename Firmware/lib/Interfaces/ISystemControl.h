#pragma once

class ISystemControl {
public:
  virtual ~ISystemControl() = default;

  /**
   * Requests a system restart.
   *
   * @return Nothing.
   */
  virtual void restart() = 0;
};

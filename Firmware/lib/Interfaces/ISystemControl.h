#pragma once

class ISystemControl {
public:
  /**
   * Releases the interface without owning system-control resources.
   *
   * @return Nothing.
   */
  virtual ~ISystemControl() = default;

  /**
   * Requests a system restart.
   *
   * @return Nothing.
   */
  virtual void restart() = 0;
};

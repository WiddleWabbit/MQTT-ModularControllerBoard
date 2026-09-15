#pragma once

/**
 * Abstracts detection of USB VBUS presence.
 */
class IUsbVbus
{
public:
  virtual ~IUsbVbus() = default;

  /**
   * Reports whether USB VBUS is currently present.
   *
   * @return True when VBUS is detected.
   */
  virtual bool isPresent() const = 0;
};

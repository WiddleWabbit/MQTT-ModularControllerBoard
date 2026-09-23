#pragma once

/**
 * Live serial-status control used by staged configuration commands.
 */
class ISerialStatusControl
{
public:
  virtual ~ISerialStatusControl() = default;

  /**
   * Enables or disables periodic status snapshots for this power-on session.
   *
   * Enabling restarts the snapshot interval. The setting is not stored here.
   *
   * @param enabled True to print on the snapshot interval.
   * @return Nothing.
   */
  virtual void setReportingEnabled(bool enabled) = 0;

  /**
   * Reports whether periodic snapshots are enabled.
   *
   * @return True when periodic snapshots are enabled.
   */
  virtual bool reportingEnabled() const = 0;

  /**
   * Writes one status snapshot immediately.
   *
   * A plugged-in link is required. Periodic reporting may be disabled.
   *
   * @return Nothing.
   */
  virtual void printStatus() = 0;
};

#pragma once

#include "ISerialStatusControl.h"

/**
 * Records status-reporting changes without printing.
 */
class FakeSerialStatusControl : public ISerialStatusControl
{
public:
  /**
   * Records the requested periodic-reporting flag.
   *
   * @param enabled True when periodic snapshots should run.
   * @return Nothing.
   */
  void setReportingEnabled(bool enabled) override
  {
    reportingEnabledFlag = enabled;
    setEnabledCount++;
  }

  /**
   * Returns the recorded periodic-reporting flag.
   *
   * @return Recorded flag.
   */
  bool reportingEnabled() const override
  {
    return reportingEnabledFlag;
  }

  /**
   * Records an immediate status print.
   *
   * @return Nothing.
   */
  void printStatus() override
  {
    printCount++;
  }

  bool reportingEnabledFlag = true;
  int setEnabledCount = 0;
  int printCount = 0;
};

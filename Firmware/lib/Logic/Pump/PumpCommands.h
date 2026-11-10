#pragma once

#include <cstdint>

#include "ModuleHost.h"

/**
 * Outcome of one pump-module command.
 */
enum class PumpQueryStatus : uint8_t
{
  Ok,
  Rejected,
  Busy,
  Failed
};

/**
 * On-wire pump state, as reported by the module.
 */
enum class PumpState : uint8_t
{
  Off,
  On,
  Fault
};

/**
 * Result of GET_PUMP_STATE, SET_PUMP, or RESET_PUMP.
 * state is valid when status is Ok.
 */
struct PumpStateResult
{
  PumpQueryStatus status;
  PumpState state;
};

/**
 * GET_PUMP_NOW sample. phase is a kPumpPhase value.
 * flags is the raw flag byte.
 */
struct PumpNow
{
  uint8_t phase;
  uint8_t flags;
  uint16_t millivolts;
  uint16_t milliamps;
  uint16_t runSeconds;
};

/**
 * Result of GET_PUMP_NOW. sample is valid when status is Ok.
 */
struct PumpNowResult
{
  PumpQueryStatus status;
  PumpNow sample;
};

/**
 * GET_PUMP_CYCLE sample. The open cycle, or the last one that closed.
 * Each field is the big-endian value from the board.
 */
struct PumpCycle
{
  uint16_t peakMillivolts;
  uint16_t minimumMillivolts;
  uint16_t peakMilliamps;
  uint16_t averageMillivolts;
  uint16_t averageMilliamps;
  uint16_t softStartMs;
  uint16_t powerGoodMs;
  uint16_t runSeconds;
};

/**
 * Result of GET_PUMP_CYCLE. sample is valid when status is Ok.
 */
struct PumpCycleResult
{
  PumpQueryStatus status;
  PumpCycle sample;
};

/**
 * GET_PUMP_TOTALS sample. Lifetime counts.
 */
struct PumpTotals
{
  uint32_t completedCycles;
  uint32_t totalSeconds;
  uint16_t peakMillivolts;
  uint16_t peakMilliamps;
  uint16_t failedStarts;
  uint16_t lastRunSeconds;
};

/**
 * Result of GET_PUMP_TOTALS. sample is valid when status is Ok.
 */
struct PumpTotalsResult
{
  PumpQueryStatus status;
  PumpTotals sample;
};

/**
 * GET_FAULT sample. reason 0 is no fault. index is 0 on this pump.
 */
struct PumpFaultReport
{
  uint8_t reason;
  uint8_t index;
};

/**
 * Result of GET_FAULT. report is valid when status is Ok.
 */
struct PumpFaultResult
{
  PumpQueryStatus status;
  PumpFaultReport report;
};

/**
 * Reads the pump on a Pump module.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the pump state, or Busy, Failed, or Rejected.
 */
PumpStateResult queryPumpState(ModuleHost& host, uint8_t slotIndex);

/**
 * Turns the pump on or off.
 * The returned state is what the module reports after the command,
 * which may still be Fault.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
PumpStateResult setPump(ModuleHost& host, uint8_t slotIndex, bool on);

/**
 * Resets the pump. Sent only when a caller asks for a reset.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
PumpStateResult resetPump(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads the live pump sample. Does not change the pump.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the sample, or Busy, Failed, or Rejected.
 */
PumpNowResult queryPumpNow(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads the open cycle, or the last cycle that closed.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the cycle, or Busy, Failed, or Rejected.
 */
PumpCycleResult queryPumpCycle(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads the lifetime totals.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the totals, or Busy, Failed, or Rejected.
 */
PumpTotalsResult queryPumpTotals(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads the latched fault reason. Sent only while the state is fault.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the reason, or Busy, Failed, or Rejected.
 */
PumpFaultResult queryPumpFault(ModuleHost& host, uint8_t slotIndex);

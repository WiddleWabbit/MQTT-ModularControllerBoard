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

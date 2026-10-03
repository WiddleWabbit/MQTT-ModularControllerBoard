#pragma once

#include <cstdint>

#include "ModuleHost.h"

/**
 * Outcome of one solenoid-module command.
 */
enum class SolenoidQueryStatus : uint8_t
{
  Ok,
  Rejected,
  Busy,
  Failed
};

/**
 * On-wire solenoid output state, as reported by the module.
 */
enum class SolenoidOutputState : uint8_t
{
  Off,
  On,
  Disconnected
};

/**
 * Result of GET_SOLENOID_COUNT.
 */
struct SolenoidCountResult
{
  SolenoidQueryStatus status;
  uint8_t count;
};

/**
 * Result of GET_SOLENOID_STATE or SET_SOLENOID.
 * state is valid when status is Ok.
 */
struct SolenoidStateResult
{
  SolenoidQueryStatus status;
  SolenoidOutputState state;
};

/**
 * Reads how many solenoid outputs a Solenoid module reports.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and a count of 0..kMaxSolenoidsPerModule, or Busy, Failed,
 *         or Rejected.
 */
SolenoidCountResult querySolenoidCount(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads one solenoid output.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param solenoidIndex Zero-based output on that module.
 * @return Ok and the output state, or Busy, Failed, or Rejected.
 */
SolenoidStateResult querySolenoidState(ModuleHost& host, uint8_t slotIndex,
                                       uint8_t solenoidIndex);

/**
 * Turns one solenoid output on or off.
 * The returned state is what the module reports after the command,
 * which may still be Disconnected.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param solenoidIndex Zero-based output on that module.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SolenoidStateResult setSolenoid(ModuleHost& host, uint8_t slotIndex,
                                uint8_t solenoidIndex, bool on);

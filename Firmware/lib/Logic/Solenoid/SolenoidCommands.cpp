#include "SolenoidCommands.h"

#include "ModuleProtocol.h"

namespace
{
/**
 * Maps a bus exchange onto the solenoid result status.
 *
 * @param status Exchange status.
 * @return Solenoid query status.
 */
SolenoidQueryStatus solenoidStatus(ModuleExchangeStatus status)
{
  switch (status)
  {
    case ModuleExchangeStatus::Ok:
      return SolenoidQueryStatus::Ok;
    case ModuleExchangeStatus::Busy:
      return SolenoidQueryStatus::Busy;
    case ModuleExchangeStatus::Rejected:
      return SolenoidQueryStatus::Rejected;
    default:
      return SolenoidQueryStatus::Failed;
  }
}

/**
 * Decodes a solenoid state byte.
 *
 * @param wire State byte from the module.
 * @param state Decoded state, unchanged when the byte is invalid.
 * @return True when wire is off, on, or disconnected.
 */
bool decodeSolenoidState(uint8_t wire, SolenoidOutputState* state)
{
  if (wire == module_protocol::kSolenoidStateOff)
  {
    *state = SolenoidOutputState::Off;
    return true;
  }
  if (wire == module_protocol::kSolenoidStateOn)
  {
    *state = SolenoidOutputState::On;
    return true;
  }
  if (wire == module_protocol::kSolenoidStateDisconnected)
  {
    *state = SolenoidOutputState::Disconnected;
    return true;
  }
  return false;
}
}


// ========== Solenoid commands ==========

/**
 * Reads how many solenoid outputs a Solenoid module reports.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and a count, or Busy, Failed, or Rejected.
 */
SolenoidCountResult querySolenoidCount(ModuleHost& host, uint8_t slotIndex)
{
  SolenoidCountResult result;
  result.status = SolenoidQueryStatus::Failed;
  result.count = 0;
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSolenoidModule,
      module_protocol::kCmdGetSolenoidCount, nullptr, 0);
  result.status = solenoidStatus(exchange.status);
  if (result.status != SolenoidQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSolenoidCountPayloadLen ||
      exchange.payload[0] > module_protocol::kMaxSolenoidsPerModule)
  {
    result.status = SolenoidQueryStatus::Failed;
    return result;
  }
  result.count = exchange.payload[0];
  return result;
}

/**
 * Reads one solenoid output.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param solenoidIndex Zero-based output on that module.
 * @return Ok and the output state, or Busy, Failed, or Rejected.
 */
SolenoidStateResult querySolenoidState(ModuleHost& host, uint8_t slotIndex,
                                       uint8_t solenoidIndex)
{
  SolenoidStateResult result;
  result.status = SolenoidQueryStatus::Rejected;
  result.state = SolenoidOutputState::Off;
  if (solenoidIndex >= module_protocol::kMaxSolenoidsPerModule)
  {
    return result;
  }
  const uint8_t request[1] = {solenoidIndex};
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSolenoidModule,
      module_protocol::kCmdGetSolenoidState, request, sizeof(request));
  result.status = solenoidStatus(exchange.status);
  if (result.status != SolenoidQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSolenoidStatePayloadLen ||
      exchange.payload[0] != solenoidIndex ||
      !decodeSolenoidState(exchange.payload[1], &result.state))
  {
    result.status = SolenoidQueryStatus::Failed;
    return result;
  }
  return result;
}

/**
 * Turns one solenoid output on or off.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param solenoidIndex Zero-based output on that module.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SolenoidStateResult setSolenoid(ModuleHost& host, uint8_t slotIndex,
                                uint8_t solenoidIndex, bool on)
{
  SolenoidStateResult result;
  result.status = SolenoidQueryStatus::Rejected;
  result.state = SolenoidOutputState::Off;
  if (solenoidIndex >= module_protocol::kMaxSolenoidsPerModule)
  {
    return result;
  }
  const uint8_t request[module_protocol::kSolenoidSetPayloadLen] = {
    solenoidIndex,
    static_cast<uint8_t>(on ? module_protocol::kSolenoidStateOn
                            : module_protocol::kSolenoidStateOff),
  };
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSolenoidModule,
      module_protocol::kCmdSetSolenoid, request, sizeof(request));
  result.status = solenoidStatus(exchange.status);
  if (result.status != SolenoidQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSolenoidStatePayloadLen ||
      exchange.payload[0] != solenoidIndex ||
      !decodeSolenoidState(exchange.payload[1], &result.state))
  {
    result.status = SolenoidQueryStatus::Failed;
    return result;
  }
  return result;
}

#include "PumpCommands.h"

#include "ModuleProtocol.h"

namespace
{
/**
 * Maps a bus exchange onto the pump result status.
 *
 * @param status Exchange status.
 * @return Pump query status.
 */
PumpQueryStatus pumpStatus(ModuleExchangeStatus status)
{
  switch (status)
  {
    case ModuleExchangeStatus::Ok:
      return PumpQueryStatus::Ok;
    case ModuleExchangeStatus::Busy:
      return PumpQueryStatus::Busy;
    case ModuleExchangeStatus::Rejected:
      return PumpQueryStatus::Rejected;
    default:
      return PumpQueryStatus::Failed;
  }
}

/**
 * Decodes a pump state byte.
 *
 * @param wire State byte from the module.
 * @param state Decoded state, unchanged when the byte is invalid.
 * @return True when wire is off, on, or fault.
 */
bool decodePumpState(uint8_t wire, PumpState* state)
{
  if (wire == module_protocol::kPumpStateOff)
  {
    *state = PumpState::Off;
    return true;
  }
  if (wire == module_protocol::kPumpStateOn)
  {
    *state = PumpState::On;
    return true;
  }
  if (wire == module_protocol::kPumpStateFault)
  {
    *state = PumpState::Fault;
    return true;
  }
  return false;
}

/**
 * Copies a decoded pump exchange into a result.
 *
 * @param exchange Bus exchange.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applyPumpExchange(const ModuleExchange& exchange, PumpStateResult* result)
{
  result->status = pumpStatus(exchange.status);
  if (result->status != PumpQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kPumpStatePayloadLen ||
      !decodePumpState(exchange.payload[0], &result->state))
  {
    result->status = PumpQueryStatus::Failed;
  }
}
}


// ========== Pump commands ==========

/**
 * Reads the pump on a Pump module.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the pump state, or Busy, Failed, or Rejected.
 */
PumpStateResult queryPumpState(ModuleHost& host, uint8_t slotIndex)
{
  PumpStateResult result;
  result.status = PumpQueryStatus::Failed;
  result.state = PumpState::Off;
  applyPumpExchange(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                                  module_protocol::kCmdGetPumpState, nullptr, 0),
                    &result);
  return result;
}

/**
 * Turns the pump on or off.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
PumpStateResult setPump(ModuleHost& host, uint8_t slotIndex, bool on)
{
  PumpStateResult result;
  result.status = PumpQueryStatus::Rejected;
  result.state = PumpState::Off;
  const uint8_t request[module_protocol::kPumpSetPayloadLen] = {
    static_cast<uint8_t>(on ? module_protocol::kPumpStateOn
                            : module_protocol::kPumpStateOff),
  };
  applyPumpExchange(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                                  module_protocol::kCmdSetPump, request,
                                  sizeof(request)),
                    &result);
  return result;
}

/**
 * Resets the pump.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
PumpStateResult resetPump(ModuleHost& host, uint8_t slotIndex)
{
  PumpStateResult result;
  result.status = PumpQueryStatus::Failed;
  result.state = PumpState::Off;
  applyPumpExchange(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                                  module_protocol::kCmdResetPump, nullptr, 0),
                    &result);
  return result;
}

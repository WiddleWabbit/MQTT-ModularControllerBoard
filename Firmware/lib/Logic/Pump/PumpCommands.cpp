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

/**
 * Reads a big-endian uint16.
 *
 * @param bytes Two bytes, high byte first.
 * @return The value.
 */
uint16_t readU16(const uint8_t* bytes)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                               bytes[1]);
}

/**
 * Reads a big-endian uint32.
 *
 * @param bytes Four bytes, high byte first.
 * @return The value.
 */
uint32_t readU32(const uint8_t* bytes)
{
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

/**
 * Decodes GET_PUMP_NOW. A phase outside 0..3 fails the exchange.
 *
 * @param exchange Bus exchange.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applyPumpNow(const ModuleExchange& exchange, PumpNowResult* result)
{
  result->status = pumpStatus(exchange.status);
  if (result->status != PumpQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kPumpNowPayloadLen ||
      exchange.payload[0] > module_protocol::kPumpPhaseFault)
  {
    result->status = PumpQueryStatus::Failed;
    return;
  }
  result->sample.phase = exchange.payload[0];
  result->sample.flags = exchange.payload[1];
  result->sample.millivolts = readU16(exchange.payload + 2);
  result->sample.milliamps = readU16(exchange.payload + 4);
  result->sample.runSeconds = readU16(exchange.payload + 6);
}

/**
 * Decodes GET_PUMP_CYCLE.
 *
 * @param exchange Bus exchange.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applyPumpCycle(const ModuleExchange& exchange, PumpCycleResult* result)
{
  result->status = pumpStatus(exchange.status);
  if (result->status != PumpQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kPumpCyclePayloadLen)
  {
    result->status = PumpQueryStatus::Failed;
    return;
  }
  result->sample.peakMillivolts = readU16(exchange.payload);
  result->sample.minimumMillivolts = readU16(exchange.payload + 2);
  result->sample.peakMilliamps = readU16(exchange.payload + 4);
  result->sample.averageMillivolts = readU16(exchange.payload + 6);
  result->sample.averageMilliamps = readU16(exchange.payload + 8);
  result->sample.softStartMs = readU16(exchange.payload + 10);
  result->sample.powerGoodMs = readU16(exchange.payload + 12);
  result->sample.runSeconds = readU16(exchange.payload + 14);
}

/**
 * Decodes GET_PUMP_TOTALS.
 *
 * @param exchange Bus exchange.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applyPumpTotals(const ModuleExchange& exchange, PumpTotalsResult* result)
{
  result->status = pumpStatus(exchange.status);
  if (result->status != PumpQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kPumpTotalsPayloadLen)
  {
    result->status = PumpQueryStatus::Failed;
    return;
  }
  result->sample.completedCycles = readU32(exchange.payload);
  result->sample.totalSeconds = readU32(exchange.payload + 4);
  result->sample.peakMillivolts = readU16(exchange.payload + 8);
  result->sample.peakMilliamps = readU16(exchange.payload + 10);
  result->sample.failedStarts = readU16(exchange.payload + 12);
  result->sample.lastRunSeconds = readU16(exchange.payload + 14);
}

/**
 * Decodes GET_FAULT.
 *
 * @param exchange Bus exchange.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applyPumpFault(const ModuleExchange& exchange, PumpFaultResult* result)
{
  result->status = pumpStatus(exchange.status);
  if (result->status != PumpQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kFaultPayloadLen)
  {
    result->status = PumpQueryStatus::Failed;
    return;
  }
  result->report.reason = exchange.payload[0];
  result->report.index = exchange.payload[1];
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

/**
 * Reads the live pump sample. Does not change the pump.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the sample, or Busy, Failed, or Rejected.
 */
PumpNowResult queryPumpNow(ModuleHost& host, uint8_t slotIndex)
{
  PumpNowResult result;
  result.status = PumpQueryStatus::Failed;
  result.sample.phase = module_protocol::kPumpPhaseOff;
  result.sample.flags = 0;
  result.sample.millivolts = 0;
  result.sample.milliamps = 0;
  result.sample.runSeconds = 0;
  applyPumpNow(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                             module_protocol::kCmdGetPumpNow, nullptr, 0),
               &result);
  return result;
}

/**
 * Reads the open cycle, or the last cycle that closed.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the cycle, or Busy, Failed, or Rejected.
 */
PumpCycleResult queryPumpCycle(ModuleHost& host, uint8_t slotIndex)
{
  PumpCycleResult result;
  result.status = PumpQueryStatus::Failed;
  result.sample.peakMillivolts = 0;
  result.sample.minimumMillivolts = 0;
  result.sample.peakMilliamps = 0;
  result.sample.averageMillivolts = 0;
  result.sample.averageMilliamps = 0;
  result.sample.softStartMs = 0;
  result.sample.powerGoodMs = 0;
  result.sample.runSeconds = 0;
  applyPumpCycle(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                               module_protocol::kCmdGetPumpCycle, nullptr, 0),
                 &result);
  return result;
}

/**
 * Reads the lifetime totals.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the totals, or Busy, Failed, or Rejected.
 */
PumpTotalsResult queryPumpTotals(ModuleHost& host, uint8_t slotIndex)
{
  PumpTotalsResult result;
  result.status = PumpQueryStatus::Failed;
  result.sample.completedCycles = 0;
  result.sample.totalSeconds = 0;
  result.sample.peakMillivolts = 0;
  result.sample.peakMilliamps = 0;
  result.sample.failedStarts = 0;
  result.sample.lastRunSeconds = 0;
  applyPumpTotals(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                                module_protocol::kCmdGetPumpTotals, nullptr,
                                0),
                  &result);
  return result;
}

/**
 * Reads the latched fault reason. Sent only while the state is fault.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and the reason, or Busy, Failed, or Rejected.
 */
PumpFaultResult queryPumpFault(ModuleHost& host, uint8_t slotIndex)
{
  PumpFaultResult result;
  result.status = PumpQueryStatus::Failed;
  result.report.reason = module_protocol::kFaultNone;
  result.report.index = 0;
  applyPumpFault(host.exchange(slotIndex, module_protocol::kTypePumpModule,
                               module_protocol::kCmdGetFault, nullptr, 0),
                 &result);
  return result;
}

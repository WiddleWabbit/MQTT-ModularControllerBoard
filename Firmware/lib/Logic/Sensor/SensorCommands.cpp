#include "SensorCommands.h"

#include "ModuleProtocol.h"

namespace
{
/**
 * Maps a bus exchange onto the sensor result status.
 *
 * @param status Exchange status.
 * @return Sensor query status.
 */
SensorQueryStatus sensorStatus(ModuleExchangeStatus status)
{
  switch (status)
  {
    case ModuleExchangeStatus::Ok:
      return SensorQueryStatus::Ok;
    case ModuleExchangeStatus::Busy:
      return SensorQueryStatus::Busy;
    case ModuleExchangeStatus::Rejected:
      return SensorQueryStatus::Rejected;
    default:
      return SensorQueryStatus::Failed;
  }
}

/**
 * Decodes a sensor state byte.
 *
 * @param wire State byte from the module.
 * @param state Decoded state, unchanged when the byte is invalid.
 * @return True when wire is off, on, fault, or disconnected.
 */
bool decodeSensorState(uint8_t wire, SensorState* state)
{
  if (wire == module_protocol::kSensorStateOff)
  {
    *state = SensorState::Off;
    return true;
  }
  if (wire == module_protocol::kSensorStateOn)
  {
    *state = SensorState::On;
    return true;
  }
  if (wire == module_protocol::kSensorStateFault)
  {
    *state = SensorState::Fault;
    return true;
  }
  if (wire == module_protocol::kSensorStateDisconnected)
  {
    *state = SensorState::Disconnected;
    return true;
  }
  return false;
}

/**
 * Copies a decoded state exchange into a result.
 * The echoed index must match the input that was asked.
 *
 * @param exchange Bus exchange.
 * @param sensorIndex Zero-based input that was requested.
 * @param result Destination. Status is set from the exchange.
 * @return Nothing.
 */
void applySensorState(const ModuleExchange& exchange, uint8_t sensorIndex,
                      SensorStateResult* result)
{
  result->status = sensorStatus(exchange.status);
  if (result->status != SensorQueryStatus::Ok)
  {
    return;
  }
  if (exchange.payloadLen != module_protocol::kSensorStatePayloadLen ||
      exchange.payload[0] != sensorIndex ||
      !decodeSensorState(exchange.payload[1], &result->state))
  {
    result->status = SensorQueryStatus::Failed;
  }
}
}


// ========== Sensor commands ==========

/**
 * Reads how many sensor inputs a Sensor module reports.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and a count, or Busy, Failed, or Rejected.
 */
SensorCountResult querySensorCount(ModuleHost& host, uint8_t slotIndex)
{
  SensorCountResult result;
  result.status = SensorQueryStatus::Failed;
  result.count = 0;
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSensorModule,
      module_protocol::kCmdGetSensorCount, nullptr, 0);
  result.status = sensorStatus(exchange.status);
  if (result.status != SensorQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSensorCountPayloadLen ||
      exchange.payload[0] > module_protocol::kMaxSensorsPerModule)
  {
    result.status = SensorQueryStatus::Failed;
    return result;
  }
  result.count = exchange.payload[0];
  return result;
}

/**
 * Reads whether one sensor input is connected.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the connected flag, or Busy, Failed, or Rejected.
 */
SensorConnectedResult querySensorConnected(ModuleHost& host, uint8_t slotIndex,
                                          uint8_t sensorIndex)
{
  SensorConnectedResult result;
  result.status = SensorQueryStatus::Rejected;
  result.connected = false;
  if (sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return result;
  }
  const uint8_t request[1] = {sensorIndex};
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSensorModule,
      module_protocol::kCmdGetSensorConnected, request, sizeof(request));
  result.status = sensorStatus(exchange.status);
  if (result.status != SensorQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSensorConnectedPayloadLen ||
      exchange.payload[0] != sensorIndex || exchange.payload[1] > 1)
  {
    result.status = SensorQueryStatus::Failed;
    return result;
  }
  result.connected = exchange.payload[1] == 1;
  return result;
}

/**
 * Reads one sensor input.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok, connected, and the raw int32 value, or Busy, Failed,
 *         or Rejected.
 */
SensorReadingResult querySensorReading(ModuleHost& host, uint8_t slotIndex,
                                       uint8_t sensorIndex)
{
  SensorReadingResult result;
  result.status = SensorQueryStatus::Rejected;
  result.connected = false;
  result.value = 0;
  if (sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return result;
  }
  const uint8_t request[1] = {sensorIndex};
  const ModuleExchange exchange = host.exchange(
      slotIndex, module_protocol::kTypeSensorModule,
      module_protocol::kCmdGetSensorReading, request, sizeof(request));
  result.status = sensorStatus(exchange.status);
  if (result.status != SensorQueryStatus::Ok)
  {
    return result;
  }
  if (exchange.payloadLen != module_protocol::kSensorReadingPayloadLen ||
      exchange.payload[0] != sensorIndex || exchange.payload[1] > 1)
  {
    result.status = SensorQueryStatus::Failed;
    return result;
  }
  result.connected = exchange.payload[1] == 1;
  result.value = module_protocol::readInt32Be(exchange.payload + 2);
  return result;
}

/**
 * Reads the on/off/fault/disconnected state of one input.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the state, or Busy, Failed, or Rejected.
 */
SensorStateResult querySensorState(ModuleHost& host, uint8_t slotIndex,
                                   uint8_t sensorIndex)
{
  SensorStateResult result;
  result.status = SensorQueryStatus::Rejected;
  result.state = SensorState::Off;
  if (sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return result;
  }
  const uint8_t request[1] = {sensorIndex};
  applySensorState(host.exchange(slotIndex, module_protocol::kTypeSensorModule,
                                 module_protocol::kCmdGetSensorState, request,
                                 sizeof(request)),
                   sensorIndex, &result);
  return result;
}

/**
 * Turns one input's loop on or off.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SensorStateResult setSensor(ModuleHost& host, uint8_t slotIndex,
                            uint8_t sensorIndex, bool on)
{
  SensorStateResult result;
  result.status = SensorQueryStatus::Rejected;
  result.state = SensorState::Off;
  if (sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return result;
  }
  const uint8_t request[module_protocol::kSensorSetPayloadLen] = {
    sensorIndex,
    static_cast<uint8_t>(on ? module_protocol::kSensorStateOn
                            : module_protocol::kSensorStateOff),
  };
  applySensorState(host.exchange(slotIndex, module_protocol::kTypeSensorModule,
                                 module_protocol::kCmdSetSensor, request,
                                 sizeof(request)),
                   sensorIndex, &result);
  return result;
}

/**
 * Resets one input. Sent only when a caller asks for a reset.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SensorStateResult resetSensor(ModuleHost& host, uint8_t slotIndex,
                              uint8_t sensorIndex)
{
  SensorStateResult result;
  result.status = SensorQueryStatus::Rejected;
  result.state = SensorState::Off;
  if (sensorIndex >= module_protocol::kMaxSensorsPerModule)
  {
    return result;
  }
  const uint8_t request[1] = {sensorIndex};
  applySensorState(host.exchange(slotIndex, module_protocol::kTypeSensorModule,
                                 module_protocol::kCmdResetSensor, request,
                                 sizeof(request)),
                   sensorIndex, &result);
  return result;
}

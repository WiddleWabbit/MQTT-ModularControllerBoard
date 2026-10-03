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

#pragma once

#include <cstdint>

#include "ModuleHost.h"
#include "ModuleProtocol.h"

/**
 * Outcome of one sensor-module command.
 */
enum class SensorQueryStatus : uint8_t
{
  Ok,
  Rejected,
  Busy,
  Failed
};

/**
 * Result of GET_SENSOR_COUNT.
 */
struct SensorCountResult
{
  SensorQueryStatus status;
  uint8_t count;
};

/**
 * Result of GET_SENSOR_CONNECTED.
 */
struct SensorConnectedResult
{
  SensorQueryStatus status;
  bool connected;
};

/**
 * Result of GET_SENSOR_READING. value is valid when status is Ok.
 */
struct SensorReadingResult
{
  SensorQueryStatus status;
  bool connected;
  int32_t value;
};

/**
 * On-wire sensor state, as reported by the module.
 */
enum class SensorState : uint8_t
{
  Off,
  On,
  Fault,
  Disconnected
};

/**
 * Result of GET_SENSOR_STATE, SET_SENSOR, or RESET_SENSOR.
 * state is valid when status is Ok.
 */
struct SensorStateResult
{
  SensorQueryStatus status;
  SensorState state;
};

/**
 * GET_FAULT list. count is the number of records. reason[i] is the
 * reason for wire index i. Reason 0 is not latched. Valid when the
 * query status is Ok.
 */
struct SensorFaultReport
{
  uint8_t count;
  uint8_t reason[module_protocol::kMaxSensorsPerModule];
};

/**
 * Result of GET_FAULT. report is valid when status is Ok.
 */
struct SensorFaultResult
{
  SensorQueryStatus status;
  SensorFaultReport report;
};

/**
 * Reads how many sensor inputs a Sensor module reports.
 * One exchange when the slot is an Online Sensor.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @return Ok and a count of 0..kMaxSensorsPerModule, Busy when the
 *         module is busy, Failed on a bad frame or bus error, or
 *         Rejected when the slot is not an Online Sensor module.
 */
SensorCountResult querySensorCount(ModuleHost& host, uint8_t slotIndex);

/**
 * Reads whether one sensor input is connected.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the connected flag, or Busy, Failed, or Rejected.
 */
SensorConnectedResult querySensorConnected(ModuleHost& host, uint8_t slotIndex,
                                          uint8_t sensorIndex);

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
                                       uint8_t sensorIndex);

/**
 * Reads the on/off/fault/disconnected state of one input.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the state, or Busy, Failed, or Rejected.
 */
SensorStateResult querySensorState(ModuleHost& host, uint8_t slotIndex,
                                   uint8_t sensorIndex);

/**
 * Turns one input's loop on or off.
 * The returned state is what the module reports after the command.
 * A fault stays faulted. Busy means the loop just closed and the
 * next sample is not stored yet.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @param on True to command on, false to command off.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SensorStateResult setSensor(ModuleHost& host, uint8_t slotIndex,
                            uint8_t sensorIndex, bool on);

/**
 * Resets one input. Sent only when a caller asks for a reset.
 * The module leaves that input off.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param sensorIndex Zero-based input on that module.
 * @return Ok and the resulting state, or Busy, Failed, or Rejected.
 */
SensorStateResult resetSensor(ModuleHost& host, uint8_t slotIndex,
                              uint8_t sensorIndex);

/**
 * Reads the fault list. The payload is one record per input, in
 * index order. expectedCount is the count the poller already stored.
 * An empty payload is accepted only when that count is 0.
 *
 * @param host Module bus.
 * @param slotIndex Firmware slot 0..3.
 * @param expectedCount Records the reply must contain, 0..16.
 * @return Ok and one reason per input, or Busy, Failed, or Rejected.
 */
SensorFaultResult querySensorFault(ModuleHost& host, uint8_t slotIndex,
                                   uint8_t expectedCount);

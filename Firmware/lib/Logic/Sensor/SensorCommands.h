#pragma once

#include <cstdint>

#include "ModuleHost.h"

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

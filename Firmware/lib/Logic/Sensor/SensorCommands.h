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

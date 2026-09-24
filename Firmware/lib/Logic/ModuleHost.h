#pragma once

#include <cstddef>
#include <cstdint>

#include "I2cMaster.h"
#include "IClock.h"
#include "SlotController.h"

/**
 * Outcome of one direct sensor-module query.
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
 * One slot's GPIO trio.
 */
struct SlotPins
{
  IDigitalPin& sense;
  IDigitalPin& mod;
  IDigitalPin& cs;
};

/**
 * Owns four slot controllers, the enumeration lock, and the I2C master.
 */
class ModuleHost
{
public:
  /**
   * Creates a module host for four slots.
   *
   * @param bus I2C master.
   * @param clock Monotonic clock.
   * @param slots Sense, MOD, and CS pins for slots 0..3.
   * @param config Timing and bus configuration.
   */
  ModuleHost(I2cMaster& bus, IClock& clock, SlotPins slots[4],
             const ModuleHostConfig& config);

  /**
   * Configures SENSE pull-ups, MOD inputs, CS pull-ups, then starts I2C.
   * No-op if already started.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Advances debounce, enumeration, and health. No-op until begin().
   * Issues at most one write/read/writeRead. May call recover() once
   * after Timeout or BusError.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Reads one slot's public state.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Public slot state, or Empty when the index is invalid.
   */
  SlotState state(uint8_t slotIndex) const;

  /**
   * Reads one slot's latched assigned address.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return 7-bit address, or 0.
   */
  uint8_t address(uint8_t slotIndex) const;

  /**
   * Reads one slot's type id.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Type id, or 0.
   */
  uint16_t typeId(uint8_t slotIndex) const;

  /**
   * Reads one slot's protocol version.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Protocol version, or 0.
   */
  uint8_t protocolVersion(uint8_t slotIndex) const;

  /**
   * Reads one slot's firmware version.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Firmware version, or 0.
   */
  uint16_t firmwareVersion(uint8_t slotIndex) const;

  /**
   * Reads one slot's identity epoch.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Epoch, or 0 before the first successful identify.
   */
  uint32_t identityEpoch(uint8_t slotIndex) const;

  /**
   * Reads one slot's last fault tag.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Fault, or None.
   */
  SlotFault fault(uint8_t slotIndex) const;

  /**
   * Reads the registered type name when the slot is Online.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Type name, or nullptr.
   */
  const char* typeName(uint8_t slotIndex) const;

  /**
   * Reads the current enumeration-lock owner.
   *
   * @return Slot index 0..3, or -1 when free.
   */
  int8_t enumLockOwner() const;

  /**
   * Issues one extra PING to an Online or Unsupported slot.
   * Not re-entrant with update(), ping(), or echo().
   *
   * @param slotIndex Firmware slot 0..3.
   * @return True when the ping succeeds.
   */
  bool ping(uint8_t slotIndex);

  /**
   * Issues IdentityEcho ECHO. False if the slot is not Online
   * IdentityEcho or inLen is greater than kMaxPayload.
   *
   * @param slotIndex Firmware slot 0..3.
   * @param in Payload to send.
   * @param inLen Payload length.
   * @param out Destination for the echoed payload.
   * @param outLen Set to the echoed length on success.
   * @return True when echo succeeds.
   */
  bool echo(uint8_t slotIndex, const uint8_t* in, size_t inLen, uint8_t* out,
            size_t* outLen);

  /**
   * Reads how many sensor inputs a Sensor module reports.
   * Not re-entrant with update(), ping(), echo(), or the other
   * sensor queries. One writeRead when the slot is an Online Sensor.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Ok and a count of 0..kMaxSensorsPerModule, Busy when the
   *         module is busy, Failed on a bad frame or bus error, or
   *         Rejected when the slot is not an Online Sensor module.
   */
  SensorCountResult querySensorCount(uint8_t slotIndex);

  /**
   * Reads whether one sensor input is connected.
   * Not re-entrant with update(), ping(), echo(), or the other
   * sensor queries.
   *
   * @param slotIndex Firmware slot 0..3.
   * @param sensorIndex Zero-based input on that module.
   * @return Ok and the connected flag, or Busy, Failed, or Rejected.
   */
  SensorConnectedResult querySensorConnected(uint8_t slotIndex,
                                             uint8_t sensorIndex);

  /**
   * Reads one sensor input.
   * Not re-entrant with update(), ping(), echo(), or the other
   * sensor queries.
   *
   * @param slotIndex Firmware slot 0..3.
   * @param sensorIndex Zero-based input on that module.
   * @return Ok, connected, and the raw int32 value, or Busy, Failed,
   *         or Rejected.
   */
  SensorReadingResult querySensorReading(uint8_t slotIndex,
                                         uint8_t sensorIndex);

  /**
   * Returns the active host configuration.
   *
   * @return Timing and bus configuration.
   */
  const ModuleHostConfig& config() const;

private:
  I2cMaster& _bus;
  IClock& _clock;
  ModuleHostConfig _config;
  SlotController _slots[4];
  bool _started = false;
  int8_t _lockOwner = -1;
  uint8_t _nextHealthSlot = 0;

  /**
   * Returns a slot pointer when the index is 0..3.
   *
   * @param slotIndex Firmware slot index.
   * @return Slot pointer, or nullptr.
   */
  SlotController* _slot(uint8_t slotIndex);

  /**
   * Returns a const slot pointer when the index is 0..3.
   *
   * @param slotIndex Firmware slot index.
   * @return Slot pointer, or nullptr.
   */
  const SlotController* _slot(uint8_t slotIndex) const;

  /**
   * Grants or releases the enumeration lock to match slot wants.
   *
   * @return Nothing.
   */
  void _reconcileLock();

  /**
   * Issues the proposed transaction and classifies the result.
   *
   * @param slotIndex Slot that proposed the operation.
   * @param proposal Proposed transaction.
   * @return Classified step result.
   */
  ModuleStepResult _issue(uint8_t slotIndex, const SlotI2cProposal& proposal);

  /**
   * Classifies a completed writeRead of a response frame.
   *
   * @param txn Bus transaction status.
   * @param rx 19-byte read buffer, valid only when txn is Ok.
   * @param expectedOkLen Expected length field for status Ok, or 0 to
   *        accept any empty-or-payload Ok frame.
   * @param ping True when the command was PING (Ok requires len == 2).
   * @param identity True when the command was GET_IDENTITY (Ok requires
   *        len == 7).
   * @return Classified step result.
   */
  ModuleStepResult _classifyRead(I2cTxnStatus txn, const uint8_t* rx,
                                 bool ping, bool identity);

  /**
   * Maps a bus error to a step result and recovers on timeout.
   *
   * @param txn Bus transaction status.
   * @return Classified step result for a non-Ok transaction.
   */
  ModuleStepResult _classifyBusError(I2cTxnStatus txn);

  /**
   * Issues one sensor-module command when the slot is Online.
   *
   * @param slotIndex Firmware slot 0..3.
   * @param cmd Sensor command byte.
   * @param txPayload Request payload, or nullptr when txLen is 0.
   * @param txLen Request payload length.
   * @param rxPayload Destination for a successful payload.
   * @param rxCap Destination capacity.
   * @param rxLen Set to the received payload length on Ok.
   * @return Query status. Ok only means the frame decoded as status Ok.
   */
  SensorQueryStatus _querySensor(uint8_t slotIndex, uint8_t cmd,
                                 const uint8_t* txPayload, size_t txLen,
                                 uint8_t* rxPayload, uint8_t rxCap,
                                 uint8_t* rxLen);
};

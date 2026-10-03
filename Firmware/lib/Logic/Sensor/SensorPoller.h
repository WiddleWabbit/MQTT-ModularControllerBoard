#pragma once

#include <cstdint>

#include "IClock.h"
#include "ModuleHost.h"
#include "ModuleProtocol.h"

/**
 * One completed on-demand sensor reading.
 * ok is false when the slot is not a usable Sensor module, the index
 * is outside the module's count, or the read failed.
 */
struct SensorDemandResult
{
  uint8_t moduleSlot;
  uint8_t sensorIndex;
  bool ok;
  bool connected;
  int32_t value;
};

/**
 * Schedules Sensor-module reads. Queries the input count when a module
 * comes online or is identified again, then presence and reading for
 * each input at pollIntervalMs. On-demand readings preempt that cycle.
 * At most one I2C query per update(). Does not publish MQTT.
 */
class SensorPoller
{
public:
  static const uint8_t kDemandQueueDepth = 4;
  static const uint8_t kSensorQueryAttempts = 3;
  static const uint32_t kDefaultPollIntervalMs = 60000;

  /**
   * Creates a poller for the four firmware slots.
   *
   * @param moduleHost Host that issues sensor queries.
   * @param clock Monotonic clock for the poll interval.
   * @param pollIntervalMs Base period for presence and reading.
   *        Zero is treated as kDefaultPollIntervalMs.
   */
  SensorPoller(ModuleHost& moduleHost, IClock& clock,
               uint32_t pollIntervalMs = kDefaultPollIntervalMs);

  /**
   * Issues at most one sensor query. No-op when nothing is due.
   * Call after ModuleHost::update() returns.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Queues an on-demand reading. Does not touch I2C.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input on that module.
   * @return False when the slot or index is out of range or the
   *         queue is full.
   */
  bool requestReading(uint8_t moduleSlot, uint8_t sensorIndex);

  /**
   * Reports whether the input count for a slot is known.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True after a successful count query for the current identity.
   */
  bool countKnown(uint8_t moduleSlot) const;

  /**
   * Reads the cached input count.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Count, or 0 when it is not known.
   */
  uint8_t sensorCount(uint8_t moduleSlot) const;

  /**
   * Reads the cached sample for one input.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param connected Set when presence has been read.
   * @param hasValue Set true when a reading has been stored.
   * @param value Set when hasValue is true.
   * @return False when the count is unknown, the index is outside
   *         that count, or presence has not been read yet.
   */
  bool sensorSample(uint8_t moduleSlot, uint8_t sensorIndex, bool* connected,
                    bool* hasValue, int32_t* value) const;

  /**
   * Reads how many readings have been stored for one input.
   * The value increases each time a reading is stored, including when
   * the raw value is unchanged. Zero means no reading is stored.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @return Reading generation, or 0 when none has been stored.
   */
  uint32_t sampleRevision(uint8_t moduleSlot, uint8_t sensorIndex) const;

  /**
   * Removes the next on-demand result.
   *
   * @param out Destination. Unchanged when the queue is empty.
   * @return True when a result was copied.
   */
  bool takeDemandResult(SensorDemandResult* out);

private:
  struct Sample
  {
    bool connectedKnown;
    bool connected;
    bool valueKnown;
    int32_t value;
    uint32_t revision;
  };

  struct SlotRuntime
  {
    uint32_t epoch;
    bool countKnown;
    uint8_t count;
    uint8_t countAttempts;
    uint32_t countRetryAt;
    bool inCycle;
    uint8_t index;
    bool readingStep;
    uint8_t stepAttempts;
    uint32_t nextPollAt;
    Sample samples[module_protocol::kMaxSensorsPerModule];
  };

  struct DemandRequest
  {
    uint8_t moduleSlot;
    uint8_t sensorIndex;
    uint8_t attempts;
  };

  ModuleHost& _moduleHost;
  IClock& _clock;
  uint32_t _intervalMs;
  SlotRuntime _runtime[module_protocol::kSlotCount];
  DemandRequest _demands[kDemandQueueDepth];
  uint8_t _demandHead;
  uint8_t _demandCount;
  SensorDemandResult _results[kDemandQueueDepth];
  uint8_t _resultHead;
  uint8_t _resultCount;

  /**
   * Forgets cached inputs when a slot is not an Online Sensor, and
   * when its identity epoch changes.
   *
   * @return Nothing.
   */
  void _syncSessions();

  /**
   * Clears one slot's cached count, cycle, and samples.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearSlot(uint8_t moduleSlot);

  /**
   * Reports whether a slot is an Online Sensor module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True when sensor queries are allowed.
   */
  bool _isActiveSensor(uint8_t moduleSlot) const;

  /**
   * Reports whether an on-demand target can never succeed.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True for Empty, Fault, Unsupported, or a different type.
   */
  bool _demandTargetIsTerminal(uint8_t moduleSlot) const;

  /**
   * Services the head of the on-demand queue.
   *
   * @return True when this update was consumed.
   */
  bool _serviceDemand();

  /**
   * Queries the input count for one module that still needs it.
   *
   * @return True when a count query was issued.
   */
  bool _serviceCount();

  /**
   * Queries the next due presence or reading.
   *
   * @return True when a periodic query was issued.
   */
  bool _servicePeriodic();

  /**
   * Finds a slot whose count query is due, preferring a demanded slot.
   *
   * @return Slot index, or -1.
   */
  int8_t _slotNeedingCount() const;

  /**
   * Finds the slot whose periodic cycle should run.
   *
   * @return Slot index, or -1.
   */
  int8_t _periodicSlot() const;

  /**
   * Reports whether an absolute due time has been reached.
   * A due time of 0 is always due.
   *
   * @param due Absolute monotonic time, or 0.
   * @return True when the time is due.
   */
  bool _isDue(uint32_t due) const;

  /**
   * Stores a presence result.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param connected Presence flag.
   * @return Nothing.
   */
  void _rememberConnected(uint8_t moduleSlot, uint8_t sensorIndex,
                          bool connected);

  /**
   * Stores a reading result, including its connected flag.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param connected Presence flag from the reading.
   * @param value Raw module value.
   * @return Nothing.
   */
  void _rememberReading(uint8_t moduleSlot, uint8_t sensorIndex,
                        bool connected, int32_t value);

  /**
   * Advances the periodic cursor after a finished step.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _advancePeriodic(uint8_t moduleSlot);

  /**
   * Appends an on-demand result, dropping the oldest if the queue is full.
   *
   * @param result Completed demand.
   * @return Nothing.
   */
  void _pushResult(const SensorDemandResult& result);

  /**
   * Removes the head on-demand request.
   *
   * @return Nothing.
   */
  void _popDemand();
};

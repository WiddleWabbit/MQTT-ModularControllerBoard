#pragma once

#include <cstdint>

#include "IClock.h"
#include "ModuleHost.h"
#include "ModuleProtocol.h"
#include "SensorCommands.h"

/**
 * One completed on-demand sensor read.
 * ok is false when the slot cannot answer, the index is outside the
 * module's count, or the read failed. state is valid when ok is true.
 * hasValue is true only for a reading taken while the input is on.
 */
struct SensorDemandResult
{
  uint8_t moduleSlot;
  uint8_t sensorIndex;
  bool ok;
  SensorState state;
  bool hasValue;
  int32_t value;
};

/**
 * Schedules Sensor-module commands. Reads each input's state when a
 * module comes online, then again at pollIntervalMs. A reading is
 * taken only while that state is on. MQTT on and off are applied when
 * the known state differs and is not fault, then the state is read
 * again before the periodic cycle. A fault is left alone until an
 * MQTT reset. After that reset, a desired on is applied when the
 * input is no longer faulted. At most one I2C query per update().
 * Does not publish MQTT.
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
   * @param pollIntervalMs Gap after a finished state cycle, and the
   *        wait after three failed queries. Zero selects
   *        kDefaultPollIntervalMs.
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
   * Records the desired on/off state for one input. Does not touch
   * I2C. A later command for the same input replaces this one.
   * The state is read again after the switch.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input on that module.
   * @param on True commands the loop closed.
   * @return False when the slot or index is out of range.
   */
  bool commandState(uint8_t moduleSlot, uint8_t sensorIndex, bool on);

  /**
   * Records a reset for one input. Does not touch I2C and does not
   * change the desired on/off state. This is the only path that
   * sends RESET_SENSOR.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input on that module.
   * @return False when the slot or index is out of range.
   */
  bool commandReset(uint8_t moduleSlot, uint8_t sensorIndex);

  /**
   * Queues an on-demand reading. Does not touch I2C. The reading is
   * sent only when the known state is on. Any other known state
   * completes with that state and no reading.
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
   * Reads the cached state for one input.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param state Set when a state has been stored.
   * @return False when the count is unknown, the index is outside
   *         that count, or the state has not been read yet.
   */
  bool sensorState(uint8_t moduleSlot, uint8_t sensorIndex,
                   SensorState* state) const;

  /**
   * Reads the cached sample for one input.
   * connected is true only when the state is on.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param connected Set when the state has been read.
   * @param hasValue Set true when a reading has been stored.
   * @param value Set when hasValue is true.
   * @return False when the count is unknown, the index is outside
   *         that count, or the state has not been read yet.
   */
  bool sensorSample(uint8_t moduleSlot, uint8_t sensorIndex, bool* connected,
                    bool* hasValue, int32_t* value) const;

  /**
   * Reads how many publishable observations have been stored.
   * The value increases when a state other than on is stored, and
   * when a reading is stored. Zero means nothing is ready to publish.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @return Observation generation, or 0 when none has been stored.
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
    bool stateKnown;
    SensorState state;
    bool valueKnown;
    int32_t value;
    uint32_t revision;
    uint8_t fetchAttempts;
    uint32_t fetchRetryAt;
  };

  struct Intent
  {
    bool desiredValid;
    bool desiredOn;
    bool applyPending;
    uint8_t applyAttempts;
    bool resetPending;
    uint8_t resetAttempts;
    bool confirmPending;
    bool confirmReads;
    uint8_t confirmAttempts;
    uint32_t confirmRetryAt;
    bool readNow;
    uint8_t readAttempts;
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
    bool staleReading;
  };

  ModuleHost& _moduleHost;
  IClock& _clock;
  uint32_t _intervalMs;
  SlotRuntime _runtime[module_protocol::kSlotCount];
  Intent _intent[module_protocol::kSlotCount]
                [module_protocol::kMaxSensorsPerModule];
  DemandRequest _demands[kDemandQueueDepth];
  uint8_t _demandHead;
  uint8_t _demandCount;
  SensorDemandResult _results[kDemandQueueDepth];
  uint8_t _resultHead;
  uint8_t _resultCount;

  /**
   * Forgets cached inputs when a slot is not an Online Sensor, and
   * when its identity epoch changes. Drops desired state when the
   * slot can never be a Sensor module.
   *
   * @return Nothing.
   */
  void _syncSessions();

  /**
   * Drops commands for inputs the module does not report.
   * Runs only after the count is known.
   *
   * @return Nothing.
   */
  void _forgetInputsPastCount();

  /**
   * Clears one slot's cached count, cycle, and samples.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearCache(uint8_t moduleSlot);

  /**
   * Forgets desired state, resets, and follow-up reads for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearIntent(uint8_t moduleSlot);

  /**
   * Reports whether a slot is an Online Sensor module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True when sensor queries are allowed.
   */
  bool _isActiveSensor(uint8_t moduleSlot) const;

  /**
   * Reports whether a slot can never host a Sensor module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True for Empty, Fault, Unsupported, or a different type.
   */
  bool _demandTargetIsTerminal(uint8_t moduleSlot) const;

  /**
   * Reports whether one input can be queried now.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @return True when the slot is an online sensor and the index is
   *         inside the known count.
   */
  bool _inputReady(uint8_t moduleSlot, uint8_t sensorIndex) const;

  /**
   * Sends one pending reset whose state is already known.
   *
   * @return True when a reset was issued.
   */
  bool _serviceReset();

  /**
   * Drives one input toward the desired state, or arms a state
   * confirm when no SET is required. Skips a fault.
   *
   * @return True when a SET was issued.
   */
  bool _serviceApply();

  /**
   * Reads the state of an input that was just switched, or that an
   * on/off command asked to confirm.
   *
   * @return True when a state query was issued.
   */
  bool _serviceConfirm();

  /**
   * Reads an input whose confirm just reported on.
   *
   * @return True when a reading was issued.
   */
  bool _serviceReadNow();

  /**
   * Reads the state of an input that a command or an immediate read
   * still needs.
   *
   * @return True when a state query was issued.
   */
  bool _serviceUnknown();

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
   * Queries the next due state, or the reading for an input whose
   * state this cycle is on.
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
   * Reports whether a known state already satisfies the desired state.
   * On and disconnected both satisfy desired on. Fault does not match.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @return True when no SET is required.
   */
  bool _matches(uint8_t moduleSlot, uint8_t sensorIndex) const;

  /**
   * Arms the state read that follows an on or off command.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param readAfter True when an on result should be followed by a
   *        reading.
   * @return Nothing.
   */
  void _armConfirm(uint8_t moduleSlot, uint8_t sensorIndex, bool readAfter);

  /**
   * Stores a state result. A poll that disagrees with the desired
   * state arms a set, except while the input is faulted.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param state State reported by the module.
   * @param fromPoll True when a mismatch should arm a set.
   * @return Nothing.
   */
  void _rememberState(uint8_t moduleSlot, uint8_t sensorIndex,
                      SensorState state, bool fromPoll);

  /**
   * Stores a reading taken while the input is on.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @param value Raw module value.
   * @return Nothing.
   */
  void _rememberReading(uint8_t moduleSlot, uint8_t sensorIndex,
                        int32_t value);

  /**
   * Drops a periodic reading that is no longer valid for this input.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param sensorIndex Zero-based input.
   * @return Nothing.
   */
  void _dropPeriodicReading(uint8_t moduleSlot, uint8_t sensorIndex);

  /**
   * Moves the periodic cursor to the next input.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _advancePeriodic(uint8_t moduleSlot);

  /**
   * Increases the publish generation, skipping 0.
   *
   * @param sample Cached input.
   * @return Nothing.
   */
  void _bump(Sample& sample);

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

  /**
   * Fills a failed on-demand result and removes the head request.
   *
   * @param demand Head request.
   * @return Nothing.
   */
  void _failDemand(const DemandRequest& demand);
};

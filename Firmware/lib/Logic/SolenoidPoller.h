#pragma once

#include <cstdint>

#include "IClock.h"
#include "ModuleHost.h"
#include "ModuleProtocol.h"

/**
 * Schedules Solenoid-module commands. Queries the output count when a
 * module comes online or is identified again, then reads each output
 * at pollIntervalMs. An MQTT desired-state command turns on or off
 * only the outputs that differ. When no command is accepted for
 * commandTimeoutMs, every output is turned off. At most one I2C query
 * per update(). Does not publish MQTT.
 */
class SolenoidPoller
{
public:
  static const uint8_t kSolenoidQueryAttempts = 3;
  static const uint32_t kDefaultPollIntervalMs = 60000;
  static const uint32_t kDefaultCommandTimeoutMs = 15UL * 60UL * 1000UL;

  /**
   * Creates a poller for the four firmware slots.
   *
   * @param moduleHost Host that issues solenoid queries.
   * @param clock Monotonic clock for the poll interval and the
   *        command-absence timeout.
   * @param pollIntervalMs Period between state polls. Zero selects
   *        kDefaultPollIntervalMs.
   * @param commandTimeoutMs Silence after which every output is
   *        commanded off. Zero selects kDefaultCommandTimeoutMs.
   *        The window starts when update() first runs, and restarts
   *        when a desired-state command is accepted.
   */
  SolenoidPoller(ModuleHost& moduleHost, IClock& clock,
                 uint32_t pollIntervalMs = kDefaultPollIntervalMs,
                 uint32_t commandTimeoutMs = kDefaultCommandTimeoutMs);

  /**
   * Issues at most one solenoid query. No-op when nothing is due.
   * Call after ModuleHost::update() returns.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Records desired on/off states for one module. Does not touch I2C.
   * Restarts the command-absence timer. A later command for the same
   * slot replaces this one. The poller applies it only when that
   * module's reported count matches count.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param desiredOn True commands on. One entry per output.
   * @param count Number of entries, 1..kMaxSolenoidsPerModule.
   * @return False when the slot, pointer, or count is out of range.
   */
  bool commandStates(uint8_t moduleSlot, const bool* desiredOn, uint8_t count);

  /**
   * Reports whether the output count for a slot is known.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True after a successful count query for the current identity.
   */
  bool countKnown(uint8_t moduleSlot) const;

  /**
   * Reads the cached output count.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Count, or 0 when it is not known.
   */
  uint8_t solenoidCount(uint8_t moduleSlot) const;

  /**
   * Reads the cached state of one output.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @param state Set when a state has been stored.
   * @return False when the count is unknown, the index is outside
   *         that count, or no state has been stored.
   */
  bool solenoidState(uint8_t moduleSlot, uint8_t solenoidIndex,
                     SolenoidOutputState* state) const;

  /**
   * Reads how many states have been stored for one output.
   * The value increases each time a state is stored, including when
   * the state is unchanged. Zero means no state is stored.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @return State generation, or 0 when none has been stored.
   */
  uint32_t stateRevision(uint8_t moduleSlot, uint8_t solenoidIndex) const;

  /**
   * Reads the poll interval in use.
   *
   * @return Poll interval in milliseconds.
   */
  uint32_t pollIntervalMs() const;

  /**
   * Reads the command-absence timeout in use.
   *
   * @return Timeout in milliseconds.
   */
  uint32_t commandTimeoutMs() const;

private:
  enum class IntentKind : uint8_t
  {
    None,
    States,
    AllOff
  };

  struct Output
  {
    bool known;
    SolenoidOutputState state;
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
    uint8_t stepAttempts;
    uint32_t nextPollAt;
    Output outputs[module_protocol::kMaxSolenoidsPerModule];
  };

  struct Intent
  {
    IntentKind kind;
    uint8_t tokenCount;
    bool desiredOn[module_protocol::kMaxSolenoidsPerModule];
    bool pending;
    bool applying;
    uint8_t index;
    uint8_t attempts;
  };

  ModuleHost& _moduleHost;
  IClock& _clock;
  uint32_t _intervalMs;
  uint32_t _commandTimeoutMs;
  bool _silenceStarted;
  uint32_t _silenceAnchor;
  bool _failsafeActive;
  SlotRuntime _runtime[module_protocol::kSlotCount];
  Intent _intent[module_protocol::kSlotCount];

  /**
   * Forgets cached outputs when a slot is not an Online Solenoid,
   * and when its identity epoch changes. Drops a desired-state
   * command when the slot can never be a Solenoid module.
   *
   * @return Nothing.
   */
  void _syncSessions();

  /**
   * Clears one slot's cached count, cycle, and outputs.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearCache(uint8_t moduleSlot);

  /**
   * Forgets a desired-state command for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearIntent(uint8_t moduleSlot);

  /**
   * Starts the silence window on the first pass, and commands every
   * output off once the window elapses.
   *
   * @return Nothing.
   */
  void _armSilence();

  /**
   * Marks one slot to be driven off. Does not restart the silence window.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _armAllOff(uint8_t moduleSlot);

  /**
   * Reports whether a slot is an Online Solenoid module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True when solenoid queries are allowed.
   */
  bool _isActiveSolenoid(uint8_t moduleSlot) const;

  /**
   * Reports whether a slot can never host a Solenoid module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True for Empty, Fault, Unsupported, or a different type.
   */
  bool _intentTargetIsTerminal(uint8_t moduleSlot) const;

  /**
   * Queries the output count for one module that still needs it.
   *
   * @return True when a count query was issued.
   */
  bool _serviceCount();

  /**
   * Drives one output toward the desired state, or reads an unknown
   * state needed to decide. Skips outputs that already match.
   *
   * @return True when a query was issued.
   */
  bool _serviceApply();

  /**
   * Reads the next due output state.
   *
   * @return True when a periodic query was issued.
   */
  bool _servicePeriodic();

  /**
   * Finds a slot whose count query is due, preferring a commanded slot.
   *
   * @return Slot index, or -1.
   */
  int8_t _slotNeedingCount() const;

  /**
   * Finds a slot with a desired state still to apply.
   *
   * @return Slot index, or -1.
   */
  int8_t _applySlot() const;

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
   * Reports the desired energised state of one output.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @return True when the output should be on.
   */
  bool _desiredOn(uint8_t moduleSlot, uint8_t solenoidIndex) const;

  /**
   * Reports whether a known state already satisfies the desired state.
   * Disconnected outputs match, because the host cannot energise them.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @return True when no SET is required.
   */
  bool _matches(uint8_t moduleSlot, uint8_t solenoidIndex) const;

  /**
   * Stores a state result.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param solenoidIndex Zero-based output.
   * @param state State reported by the module.
   * @return Nothing.
   */
  void _rememberState(uint8_t moduleSlot, uint8_t solenoidIndex,
                      SolenoidOutputState state);

  /**
   * Advances the periodic cursor after a finished step.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _advancePeriodic(uint8_t moduleSlot);
};

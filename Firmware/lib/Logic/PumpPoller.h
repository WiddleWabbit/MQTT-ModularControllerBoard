#pragma once

#include <cstdint>

#include "IClock.h"
#include "ModuleHost.h"
#include "ModuleProtocol.h"

/**
 * Schedules Pump-module commands. Reads the pump state when a module
 * comes online or is identified again, then again at pollIntervalMs.
 * An MQTT desired-state command turns the pump on or off only when
 * the known state differs. A fault is left alone until an MQTT reset.
 * After that reset, the desired state is applied when the pump is no
 * longer faulted. When no on/off command is accepted for
 * commandTimeoutMs, a pump that is on is turned off. At most one I2C
 * query per update(). Does not publish MQTT.
 */
class PumpPoller
{
public:
  static const uint8_t kPumpQueryAttempts = 3;
  static const uint32_t kDefaultPollIntervalMs = 60000;
  static const uint32_t kDefaultCommandTimeoutMs = 3UL * 60UL * 1000UL;

  /**
   * Creates a poller for the four firmware slots.
   *
   * @param moduleHost Host that issues pump queries.
   * @param clock Monotonic clock for the poll interval and the
   *        command-absence timeout.
   * @param pollIntervalMs Period between state polls. Zero selects
   *        kDefaultPollIntervalMs.
   * @param commandTimeoutMs Silence after which a pump that is on is
   *        commanded off. Zero selects kDefaultCommandTimeoutMs.
   *        The window starts when update() first runs, and restarts
   *        when a desired on/off command is accepted. A reset does
   *        not restart it.
   */
  PumpPoller(ModuleHost& moduleHost, IClock& clock,
             uint32_t pollIntervalMs = kDefaultPollIntervalMs,
             uint32_t commandTimeoutMs = kDefaultCommandTimeoutMs);

  /**
   * Issues at most one pump query. No-op when nothing is due.
   * Call after ModuleHost::update() returns.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Records the desired on/off state for one module. Does not touch
   * I2C. Restarts the command-absence timer. A later command for the
   * same slot replaces this one.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param on True commands on.
   * @return False when the slot is out of range.
   */
  bool commandState(uint8_t moduleSlot, bool on);

  /**
   * Records a reset for one module. Does not touch I2C and does not
   * restart the command-absence timer. The desired on/off state is
   * left as it was.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return False when the slot is out of range.
   */
  bool commandReset(uint8_t moduleSlot);

  /**
   * Reports whether the pump state for a slot is known.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True after a successful state query for the current identity.
   */
  bool stateKnown(uint8_t moduleSlot) const;

  /**
   * Reads the cached pump state.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param state Set when a state has been stored.
   * @return False when no state has been stored.
   */
  bool pumpState(uint8_t moduleSlot, PumpState* state) const;

  /**
   * Reads how many states have been stored for one pump.
   * The value increases each time a state is stored, including when
   * the state is unchanged. Zero means no state is stored.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return State generation, or 0 when none has been stored.
   */
  uint32_t stateRevision(uint8_t moduleSlot) const;

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
  struct SlotRuntime
  {
    uint32_t epoch;
    bool stateKnown;
    PumpState state;
    uint32_t revision;
    uint8_t readAttempts;
    uint32_t readRetryAt;
    uint32_t nextPollAt;
  };

  struct Intent
  {
    bool desiredValid;
    bool desiredOn;
    bool applyPending;
    uint8_t applyAttempts;
    bool resetPending;
    uint8_t resetAttempts;
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
   * Forgets a cached state when a slot is not an Online Pump, and
   * when its identity epoch changes. Drops a command when the slot
   * can never be a Pump module.
   *
   * @return Nothing.
   */
  void _syncSessions();

  /**
   * Clears one slot's cached state and poll timer.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearCache(uint8_t moduleSlot);

  /**
   * Forgets the desired state and any pending reset for one slot.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _clearIntent(uint8_t moduleSlot);

  /**
   * Starts the silence window on the first pass, and commands every
   * pump off once the window elapses.
   *
   * @return Nothing.
   */
  void _armSilence();

  /**
   * Marks one slot to be driven off. Does not restart the silence
   * window and does not cancel a pending reset.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return Nothing.
   */
  void _armOff(uint8_t moduleSlot);

  /**
   * Reports whether a slot is an Online Pump module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True when pump queries are allowed.
   */
  bool _isActivePump(uint8_t moduleSlot) const;

  /**
   * Reports whether a slot can never host a Pump module.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True for Empty, Fault, Unsupported, or a different type.
   */
  bool _intentTargetIsTerminal(uint8_t moduleSlot) const;

  /**
   * Reads the pump state for one module that does not have one yet.
   *
   * @return True when a state query was issued.
   */
  bool _serviceUnknown();

  /**
   * Sends one pending reset.
   *
   * @return True when a reset was issued.
   */
  bool _serviceReset();

  /**
   * Drives one pump toward the desired state. Skips a pump that
   * already matches, and a pump whose known state is fault.
   *
   * @return True when a set was issued.
   */
  bool _serviceApply();

  /**
   * Reads one due pump state.
   *
   * @return True when a periodic query was issued.
   */
  bool _servicePeriodic();

  /**
   * Finds a slot whose first state query is due, preferring a
   * commanded slot.
   *
   * @return Slot index, or -1.
   */
  int8_t _slotNeedingState() const;

  /**
   * Finds the slot whose periodic read should run.
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
   * A fault does not match, because on and off are not sent while
   * the pump is faulted.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @return True when no SET is required.
   */
  bool _matches(uint8_t moduleSlot) const;

  /**
   * Stores a state result. A poll that disagrees with the desired
   * state arms another set, except while the pump is faulted.
   *
   * @param moduleSlot Firmware slot 0..3.
   * @param state State reported by the module.
   * @param fromPoll True when this store came from a read or a reset,
   *        so a mismatch can arm a set.
   * @return Nothing.
   */
  void _rememberState(uint8_t moduleSlot, PumpState state, bool fromPoll);
};

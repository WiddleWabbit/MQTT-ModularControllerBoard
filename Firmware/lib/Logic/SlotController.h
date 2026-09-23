#pragma once

#include <cstdint>

#include "IClock.h"
#include "IDigitalPin.h"
#include "ModuleProtocol.h"

/**
 * Timing for presence, enumeration, and health pings.
 */
struct ModuleHostConfig
{
  uint32_t presenceDebounceMs = 50;
  uint32_t bootWaitMs = 200;
  uint32_t modSettleMs = 10;
  uint32_t i2cTimeoutMs = 50;
  uint32_t i2cClockHz = 100000;
  uint8_t commandRetries = 3;
  uint32_t retryGapMs = 50;
  uint32_t faultRetryMs = 1000;
  uint32_t healthPingMs = 1000;
  uint8_t healthFailLimit = 3;
};

/**
 * Public snapshot of one slot, used by serial status and callers.
 */
enum class SlotState : uint8_t
{
  Empty,
  Debouncing,
  Enumerating,
  Online,
  Unsupported,
  Fault
};

/**
 * Last classified I2C/protocol failure, or None on success.
 */
enum class SlotFault : uint8_t
{
  None,
  Nack,
  BadCrc,
  BadFrame,
  Timeout,
  Busy
};

/**
 * I2C operation a slot wants the host to issue.
 */
enum class SlotI2cOp : uint8_t
{
  None = 0,
  PingUnconfigured,
  SetAddress,
  PingAssigned,
  GetIdentity,
  HealthPing
};

/**
 * One proposed bus transaction for the host scheduler.
 */
struct SlotI2cProposal
{
  SlotI2cOp op;
  uint8_t i2cAddress;
};

/**
 * Classified result of one host-issued protocol transaction.
 */
struct ModuleStepResult
{
  SlotFault status;
  uint8_t cmdOrStatus;
  uint8_t payload[module_protocol::kMaxPayload];
  uint8_t payloadLen;
};

/**
 * Owns one slot's GPIO, debounce, MOD select, and enumeration state.
 * Does not call I2cMaster; the host issues proposed transactions.
 */
class SlotController
{
public:
  /**
   * Creates a slot controller for one motherboard slot.
   *
   * @param sense Presence pin (LOW = present).
   * @param mod Enumeration-select pin.
   * @param cs SPI chip-select pin (idle pull-up only).
   * @param clock Monotonic clock.
   * @param slotIndex Firmware slot 0..3.
   * @param config Timing configuration.
   */
  SlotController(IDigitalPin& sense, IDigitalPin& mod, IDigitalPin& cs,
                 IClock& clock, uint8_t slotIndex,
                 const ModuleHostConfig& config);

  /**
   * Sets SENSE to input-pullup, MOD to input, and CS to input-pullup.
   *
   * @return Nothing.
   */
  void configureIdlePins();

  /**
   * Advances GPIO, debounce, timers, and MOD. No I2C.
   *
   * @return Nothing.
   */
  void updatePinsAndState();

  /**
   * Reports whether this slot needs the host enumeration lock.
   *
   * @return True when the slot is waiting for or holding the lock.
   */
  bool wantsEnumLock() const;

  /**
   * Tells the slot whether it currently holds the enumeration lock.
   *
   * @param held True when this slot owns the lock.
   * @return Nothing.
   */
  void notifyEnumLock(bool held);

  /**
   * Returns the I2C operation due this tick, or None.
   *
   * @return Proposed transaction.
   */
  SlotI2cProposal proposedI2c() const;

  /**
   * Applies the classified result of the host's one transaction.
   *
   * @param result Classified protocol result.
   * @return Nothing.
   */
  void applyI2cResult(const ModuleStepResult& result);

  /**
   * Reads the public snapshot state.
   *
   * @return Public slot state.
   */
  SlotState state() const;

  /**
   * Reads the latched assigned address.
   *
   * @return 7-bit address, or 0 before VerifyAssigned success.
   */
  uint8_t address() const;

  /**
   * Reads the last committed SET_ADDRESS target.
   *
   * @return 7-bit address, or 0 if SET_ADDRESS never succeeded.
   */
  uint8_t committedAddress() const;

  /**
   * Reads the identified type id.
   *
   * @return Type id, or 0 if unknown.
   */
  uint16_t typeId() const;

  /**
   * Reads the identified protocol version.
   *
   * @return Protocol version, or 0 if unknown.
   */
  uint8_t protocolVersion() const;

  /**
   * Reads the identified firmware version.
   *
   * @return Firmware version, or 0 if unknown.
   */
  uint16_t firmwareVersion() const;

  /**
   * Reads the last fault tag.
   *
   * @return Fault, or None.
   */
  SlotFault fault() const;

  /**
   * Reads the registered type name when Online.
   *
   * @return Type name, or nullptr.
   */
  const char* typeName() const;

  /**
   * Returns the slot-derived address this slot will assign.
   *
   * @return Address 0x10 plus slot index.
   */
  uint8_t targetAddress() const;

private:
  enum class Phase : uint8_t
  {
    Empty,
    DebouncePresent,
    BootWait,
    WaitForLock,
    SelectAssert,
    ProbeDefault,
    SetAddress,
    VerifyAssigned,
    Identify,
    Online,
    Unsupported,
    Fault,
    RecoverWaitLock,
    RecoverSelect,
    RecoverProbeDefault,
    RecoverProbeAssigned,
    DebounceAbsent
  };

  IDigitalPin& _sense;
  IDigitalPin& _mod;
  IDigitalPin& _cs;
  IClock& _clock;
  uint8_t _slotIndex;
  ModuleHostConfig _config;
  Phase _phase = Phase::Empty;
  Phase _phaseBeforeAbsent = Phase::Empty;
  SlotState _priorPublic = SlotState::Empty;
  bool _lockHeld = false;
  bool _waitingRetry = false;
  uint8_t _attempts = 0;
  uint8_t _healthFails = 0;
  uint8_t _address = 0;
  uint8_t _committedAddress = 0;
  uint16_t _typeId = 0;
  uint8_t _protocolVersion = 0;
  uint16_t _firmwareVersion = 0;
  SlotFault _fault = SlotFault::None;
  uint32_t _phaseStartedAt = 0;
  uint32_t _retryAt = 0;
  uint32_t _lastHealthAt = 0;

  /**
   * Tests elapsed time using wrap-safe unsigned arithmetic.
   *
   * @param now Current monotonic time.
   * @param since Start time.
   * @param duration Required duration.
   * @return True when duration has elapsed.
   */
  static bool _hasElapsed(uint32_t now, uint32_t since, uint32_t duration);

  /**
   * Maps an internal phase to the public snapshot.
   *
   * @param phase Internal phase.
   * @return Public slot state.
   */
  static SlotState _publicOf(Phase phase);

  /**
   * Reports whether a phase is part of lock-held enumeration.
   *
   * @param phase Internal phase.
   * @return True when the phase must hold the enum lock.
   */
  static bool _holdsLock(Phase phase);

  /**
   * Reports whether sense currently indicates a seated module.
   *
   * @return True when the sense pin is LOW.
   */
  bool _isPresent() const;

  /**
   * Reports whether the current enumeration/recover step may issue I2C.
   *
   * @param now Current monotonic time.
   * @return True when an attempt is due.
   */
  bool _stepDue(uint32_t now) const;

  /**
   * Enters Empty, releases MOD, and forgets identity and addresses.
   *
   * @return Nothing.
   */
  void _enterEmpty();

  /**
   * Asserts MOD open-drain LOW and starts the settle timer.
   *
   * @return Nothing.
   */
  void _assertSelect();

  /**
   * Releases MOD to DigitalInput.
   *
   * @return Nothing.
   */
  void _releaseMod();

  /**
   * Starts a new I2C step with attempt count zero.
   *
   * @param phase New internal phase.
   * @return Nothing.
   */
  void _enterStep(Phase phase);

  /**
   * Records a failed attempt and faults after commandRetries.
   *
   * @param status Classified failure.
   * @return Nothing.
   */
  void _noteAttemptFail(SlotFault status);

  /**
   * Enters Fault, records the tag, and stops wanting the lock.
   *
   * @param status Fault tag.
   * @return Nothing.
   */
  void _enterFault(SlotFault status);

  /**
   * Parses a successful GET_IDENTITY payload into Online or Unsupported.
   *
   * @param result Identity step result.
   * @return Nothing.
   */
  void _applyIdentity(const ModuleStepResult& result);

  /**
   * Clears identity RAM while keeping committedAddress.
   *
   * @return Nothing.
   */
  void _forgetIdentity();
};

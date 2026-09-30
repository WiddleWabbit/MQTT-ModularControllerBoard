#pragma once

#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IDigitalPin.h"
#include "IspProgrammer.h"
#include "ModuleHost.h"

/**
 * Exclusive programming session. Quiesces the module host, holds reset
 * idle high, and ends after the port has been seen and then stays
 * unplugged for unplugTimeoutMs, or after no STK500 byte arrives for
 * idleTimeoutMs.
 */
class ProgrammingSession
{
public:
  /**
   * Creates a session around an ISP programmer and the module host.
   *
   * @param programmer STK500 programmer that shares resetPin.
   * @param host Module host to quiesce for the session.
   * @param resetPin ISP reset, idle high while waiting.
   * @param port Byte port watched for unplug.
   * @param clock Monotonic clock for the idle and unplug timeouts.
   * @param idleTimeoutMs Silence that ends the session.
   * @param unplugTimeoutMs Absence, after the port has been seen, that
   *        ends the session.
   */
  ProgrammingSession(IspProgrammer& programmer, ModuleHost& host,
                     IDigitalPin& resetPin, IBytePort& port, IClock& clock,
                     uint32_t idleTimeoutMs, uint32_t unplugTimeoutMs);

  /**
   * Quiesces the host and drives reset high. No-op when already active.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Services the programmer. Ends after a confirmed unplug or idle timeout.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Reports whether the session owns the bus and the USB byte stream.
   *
   * @return True until end.
   */
  bool active() const;

private:
  IspProgrammer& _programmer;
  ModuleHost& _host;
  IDigitalPin& _reset;
  IBytePort& _port;
  IClock& _clock;
  uint32_t _idleTimeoutMs;
  uint32_t _unplugTimeoutMs;
  uint32_t _idleMark = 0;
  uint32_t _unplugMark = 0;
  uint32_t _lastActivity = 0;
  bool _seenPlugged = false;
  bool _unplugTiming = false;
  bool _active = false;

  /**
   * Restores reset and the module host.
   *
   * @return Nothing.
   */
  void _finish();
};

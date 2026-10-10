#pragma once

#include <cstdint>

#include "IBytePort.h"
#include "IClock.h"
#include "IProgrammer.h"
#include "ModuleHost.h"

/**
 * Exclusive programming session. Quiesces the module host and ends
 * after the port has been seen and then stays unplugged for
 * unplugTimeoutMs, or after no protocol byte arrives for idleTimeoutMs.
 * The programmer binds its own pin in start(), before begin().
 */
class ProgrammingSession
{
public:
  /**
   * Creates a session around the module host and the USB byte port.
   *
   * @param host Module host to quiesce for the session.
   * @param port Byte port watched for unplug and for protocol bytes.
   * @param clock Monotonic clock for the idle and unplug timeouts.
   * @param idleTimeoutMs Silence that ends the session.
   * @param unplugTimeoutMs Absence, after the port has been seen, that
   *        ends the session.
   */
  ProgrammingSession(ModuleHost& host, IBytePort& port, IClock& clock,
                     uint32_t idleTimeoutMs, uint32_t unplugTimeoutMs);

  /**
   * Quiesces the host and hands it the programmer. No-op when already
   * active. Does not drive the programming pin.
   *
   * @param programmer ISP or UPDI programmer for this session.
   * @return Nothing.
   */
  void begin(IProgrammer& programmer);

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
  ModuleHost& _host;
  IBytePort& _port;
  IClock& _clock;
  IProgrammer* _programmer = nullptr;
  uint32_t _idleTimeoutMs;
  uint32_t _unplugTimeoutMs;
  uint32_t _idleMark = 0;
  uint32_t _unplugMark = 0;
  uint32_t _lastActivity = 0;
  bool _seenPlugged = false;
  bool _unplugTiming = false;
  bool _active = false;

  /**
   * Releases the programmer and resumes the module host.
   *
   * @return Nothing.
   */
  void _finish();
};

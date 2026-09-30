#include "ProgrammingSession.h"

// ========== Construction ==========

ProgrammingSession::ProgrammingSession(IspProgrammer& programmer,
                                       ModuleHost& host, IDigitalPin& resetPin,
                                       IBytePort& port, IClock& clock,
                                       uint32_t idleTimeoutMs)
  : _programmer(programmer),
    _host(host),
    _reset(resetPin),
    _port(port),
    _clock(clock),
    _idleTimeoutMs(idleTimeoutMs)
{
}


// ========== Public API ==========

/**
 * Quiesces the host and drives reset high. No-op when already active.
 *
 * @return Nothing.
 */
void ProgrammingSession::begin()
{
  if (_active)
  {
    return;
  }
  _host.quiesce();
  _reset.setMode(PinMode::DigitalOutput);
  _reset.write(true);
  _idleMark = _clock.millis();
  _lastActivity = _programmer.activityCount();
  _active = true;
}

/**
 * Services the programmer, then ends on unplug or idle timeout.
 *
 * @return Nothing.
 */
void ProgrammingSession::update()
{
  if (!_active)
  {
    return;
  }
  if (!_port.isPlugged())
  {
    _finish();
    return;
  }
  _programmer.update();
  const uint32_t activity = _programmer.activityCount();
  if (activity != _lastActivity)
  {
    _lastActivity = activity;
    _idleMark = _clock.millis();
  }
  if (static_cast<uint32_t>(_clock.millis() - _idleMark) >= _idleTimeoutMs)
  {
    _finish();
  }
}

/**
 * Reports whether the session owns the bus and the USB byte stream.
 *
 * @return True until end.
 */
bool ProgrammingSession::active() const
{
  return _active;
}


// ========== Shutdown ==========

/**
 * Restores reset and the module host.
 *
 * @return Nothing.
 */
void ProgrammingSession::_finish()
{
  _programmer.shutdown();
  _host.resume();
  _active = false;
}

#include "ProgrammingSession.h"

// ========== Construction ==========

ProgrammingSession::ProgrammingSession(ModuleHost& host, IBytePort& port,
                                       IClock& clock, uint32_t idleTimeoutMs,
                                       uint32_t unplugTimeoutMs)
  : _host(host),
    _port(port),
    _clock(clock),
    _idleTimeoutMs(idleTimeoutMs),
    _unplugTimeoutMs(unplugTimeoutMs)
{
}


// ========== Public API ==========

/**
 * Quiesces the host. No-op when already active. The pin is left as
 * start() set it.
 *
 * @param programmer ISP or UPDI programmer for this session.
 * @return Nothing.
 */
void ProgrammingSession::begin(IProgrammer& programmer)
{
  if (_active)
  {
    return;
  }
  _programmer = &programmer;
  _host.quiesce();
  _seenPlugged = false;
  _unplugTiming = false;
  _idleMark = _clock.millis();
  _lastActivity = _programmer->activityCount();
  _active = true;
}

/**
 * Services the programmer. Ends after a confirmed unplug or idle timeout.
 * A missing link ends the session only after the port has been seen in
 * this session and has then stayed absent for the unplug interval.
 * Until then, silence is the only end.
 *
 * @return Nothing.
 */
void ProgrammingSession::update()
{
  if (!_active || _programmer == nullptr)
  {
    return;
  }

  const uint32_t now = _clock.millis();
  if (_port.isPlugged())
  {
    _seenPlugged = true;
    _unplugTiming = false;
  }
  else if (_seenPlugged)
  {
    if (!_unplugTiming)
    {
      _unplugTiming = true;
      _unplugMark = now;
    }
    if (static_cast<uint32_t>(now - _unplugMark) >= _unplugTimeoutMs)
    {
      _finish();
      return;
    }
  }

  _programmer->update();
  const uint32_t activity = _programmer->activityCount();
  if (activity != _lastActivity)
  {
    _lastActivity = activity;
    _idleMark = now;
  }
  if (static_cast<uint32_t>(now - _idleMark) >= _idleTimeoutMs)
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
 * Releases the programmer and resumes the module host.
 *
 * @return Nothing.
 */
void ProgrammingSession::_finish()
{
  if (_programmer != nullptr)
  {
    _programmer->shutdown();
  }
  _host.resume();
  _active = false;
}

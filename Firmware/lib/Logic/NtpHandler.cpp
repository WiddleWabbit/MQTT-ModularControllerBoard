#include "NtpHandler.h"

NtpHandler::NtpHandler(IWifiStation& wifi, IClock& clock, INtpClient& ntp,
                       const char* serverName,
                       unsigned long updateFrequencyMs)
  : _wifi(wifi),
    _clock(clock),
    _ntp(ntp),
    _serverName(serverName == nullptr || serverName[0] == '\0'
                    ? "pool.ntp.org"
                    : serverName),
    _updateFrequencyMs(updateFrequencyMs == 0
                           ? DefaultUpdateFrequencyMs
                           : updateFrequencyMs)
{
}

void NtpHandler::update()
{
  const bool connected = _wifi.isConnected();
  if (!connected) {
    _wasConnected = false;
    _hasRequested = false;
    return;
  }

  _ntp.update();
  const unsigned long now = _clock.nowMillis();
  const bool firstConnection = !_wasConnected;
  const bool periodicUpdate =
      _hasRequested && (now - _lastRequestAt >= _updateFrequencyMs);

  if (firstConnection || periodicUpdate) {
    _ntp.requestSync(_serverName.c_str());
    _lastRequestAt = now;
    _hasRequested = true;
  }

  _wasConnected = true;
}

bool NtpHandler::setServer(const char* serverName)
{
  if (serverName == nullptr || serverName[0] == '\0') {
    return false;
  }

  _serverName = serverName;
  return true;
}

bool NtpHandler::setUpdateFrequencyMs(unsigned long updateFrequencyMs)
{
  if (updateFrequencyMs == 0) {
    return false;
  }

  _updateFrequencyMs = updateFrequencyMs;
  return true;
}

const char* NtpHandler::server() const
{
  return _serverName.c_str();
}

unsigned long NtpHandler::updateFrequencyMs() const
{
  return _updateFrequencyMs;
}

bool NtpHandler::isSynchronized() const
{
  return _wifi.isConnected() && _ntp.isSynchronized();
}

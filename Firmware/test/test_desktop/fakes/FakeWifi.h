#pragma once

#include <string>
#include <vector>

#include "IWifi.h"

class FakeWifi : public IWifi
{
public:
  /**
   * Starts a simulated WiFi connection attempt.
   *
   * @param ssid Network name.
   * @param password Network password.
   * @return Nothing.
   */
  void begin(const char* ssid, const char* password) override
  {
    beginCallCount++;
    lastSsid = ssid == nullptr ? "" : ssid;
    lastPassword = password == nullptr ? "" : password;
  }

  /**
   * Returns the simulated link state.
   *
   * @return Current simulated WiFi state.
   */
  WifiLinkState status() const override
  {
    if (statusSequenceIndex < statusSequence.size())
    {
      return statusSequence[statusSequenceIndex++];
    }
    return linkState;
  }

  /**
   * Records a simulated disconnect.
   *
   * @return Nothing.
   */
  void disconnect() override
  {
    disconnectCallCount++;
    linkState = WifiLinkState::Disconnected;
  }

  WifiLinkState linkState = WifiLinkState::Disconnected;
  int beginCallCount = 0;
  int disconnectCallCount = 0;
  std::string lastSsid;
  std::string lastPassword;
  std::vector<WifiLinkState> statusSequence;

private:
  mutable size_t statusSequenceIndex = 0;
};

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
    calls.emplace_back("begin");
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
   * Returns the simulated signal strength.
   *
   * @return Simulated RSSI in dBm.
   */
  int32_t rssi() const override
  {
    return rssiDbm;
  }

  /**
   * Returns the simulated station address.
   *
   * @return Simulated IPv4 address.
   */
  Ipv4Address localIp() const override
  {
    return localAddress;
  }

  /**
   * Records a hostname committed before the next station start.
   *
   * @param hostname Hostname text.
   * @return Nothing.
   */
  void setHostname(const char* hostname) override
  {
    setHostnameCount++;
    calls.emplace_back("hostname");
    lastHostname = hostname == nullptr ? "" : hostname;
  }

  /**
   * Records a station-mode reset used to commit a hostname.
   *
   * @return Nothing.
   */
  void resetStationMode() override
  {
    resetStationModeCount++;
    calls.emplace_back("reset");
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
  int32_t rssiDbm = 0;
  Ipv4Address localAddress{{0, 0, 0, 0}};
  int beginCallCount = 0;
  int disconnectCallCount = 0;
  int setHostnameCount = 0;
  int resetStationModeCount = 0;
  std::string lastSsid;
  std::string lastPassword;
  std::string lastHostname;
  std::vector<std::string> calls;
  std::vector<WifiLinkState> statusSequence;

private:
  mutable size_t statusSequenceIndex = 0;
};

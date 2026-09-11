#pragma once

#include <string>

#include "IWifiStation.h"

class FakeWifiStation : public IWifiStation {
public:
  /**
   * Records a disconnect request and clears the simulated connection.
   *
   * @return Nothing.
   */
  void disconnect() override
  {
    disconnectCallCount++;
    connected = false;
  }

  /**
   * Records station credentials for later test inspection.
   *
   * @param ssid Network name supplied by the system under test.
   * @param password Password supplied by the system under test.
   * @return Nothing.
   */
  void begin(const char* ssid, const char* password) override
  {
    beginCallCount++;
    lastSsid = ssid == nullptr ? "" : ssid;
    lastPassword = password == nullptr ? "" : password;
  }

  /**
   * Returns the controllable simulated connection state.
   *
   * @return True when the fake is connected.
   */
  bool isConnected() const override
  {
    return connected;
  }

  bool connected = false;
  unsigned int disconnectCallCount = 0;
  unsigned int beginCallCount = 0;
  std::string lastSsid;
  std::string lastPassword;
};

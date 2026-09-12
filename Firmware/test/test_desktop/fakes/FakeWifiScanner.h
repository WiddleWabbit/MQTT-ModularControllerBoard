#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "IWifiScanner.h"

class FakeWifiScanner : public IWifiScanner {
public:
  /**
   * Returns the configured scan result and records the request.
   *
   * @param networks Destination array supplied by the system under test.
   * @param maximumNetworks Capacity of the destination array.
   * @return Configured scan result, or a negative failure code.
   */
  int scanNetworks(WifiNetworkInfo* networks,
                   size_t maximumNetworks) override
  {
    scanCallCount++;
    lastMaximumNetworks = maximumNetworks;

    if (scanResult < 0) {
      return scanResult;
    }

    const size_t copyCount =
        std::min(static_cast<size_t>(scanResult), networksToReturn.size());
    const size_t boundedCount = std::min(copyCount, maximumNetworks);
    for (size_t index = 0; index < boundedCount; ++index) {
      networks[index] = networksToReturn[index];
    }

    return scanResult;
  }

  /**
   * Adds one network to the next simulated scan result.
   *
   * @param ssid Network name, truncated to the interface limit.
   * @param rssi Signal strength in dBm.
   * @param encrypted True when the network requires credentials.
   * @return Nothing.
   */
  void addNetwork(const char* ssid, int rssi, bool encrypted)
  {
    WifiNetworkInfo network = {};
    if (ssid != nullptr) {
      std::strncpy(network.ssid, ssid, WifiNetworkInfo::maximumSsidLength);
      network.ssid[WifiNetworkInfo::maximumSsidLength] = '\0';
    }
    network.rssi = rssi;
    network.encrypted = encrypted;
    networksToReturn.push_back(network);
    scanResult = static_cast<int>(networksToReturn.size());
  }

  int scanResult = 0;
  unsigned int scanCallCount = 0;
  size_t lastMaximumNetworks = 0;
  std::vector<WifiNetworkInfo> networksToReturn;
};

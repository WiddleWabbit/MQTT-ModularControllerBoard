#pragma once

#include <cstddef>

struct WifiNetworkInfo {
  static constexpr size_t maximumSsidLength = 32;

  char ssid[maximumSsidLength + 1] = {};
  int rssi = 0;
  bool encrypted = false;
};

class IWifiScanner {
public:
  virtual ~IWifiScanner() = default;

  /**
   * Scans for nearby wireless networks.
   *
   * @param networks Destination array for scan results.
   * @param maximumNetworks Capacity of the destination array.
   * @return Number of results, or a negative value when scanning fails.
   */
  virtual int scanNetworks(WifiNetworkInfo* networks,
                           size_t maximumNetworks) = 0;
};

#pragma once

#include "IWifiScanner.h"

class Esp32WifiScanner : public IWifiScanner {
public:
  /**
   * Scans nearby networks using the ESP32 WiFi radio.
   *
   * @param networks Destination array for scan results.
   * @param maximumNetworks Capacity of the destination array.
   * @return Number of radio results, or a negative scan error.
   */
  int scanNetworks(WifiNetworkInfo* networks,
                   size_t maximumNetworks) override;
};

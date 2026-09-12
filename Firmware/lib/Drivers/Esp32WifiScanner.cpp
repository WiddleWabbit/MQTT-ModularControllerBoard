#include "Esp32WifiScanner.h"

#include <algorithm>
#include <cstring>

#ifdef ARDUINO

#include <WiFi.h>

int Esp32WifiScanner::scanNetworks(WifiNetworkInfo* networks,
                                    size_t maximumNetworks)
{
  if (networks == nullptr || maximumNetworks == 0) {
    return 0;
  }

  WiFi.mode(WIFI_STA);
  const int result = WiFi.scanNetworks(false, false);
  if (result < 0) {
    return result;
  }

  const size_t copyCount =
      std::min(static_cast<size_t>(result), maximumNetworks);
  for (size_t index = 0; index < copyCount; ++index) {
    const String ssid = WiFi.SSID(static_cast<int>(index));
    std::strncpy(networks[index].ssid, ssid.c_str(),
                 WifiNetworkInfo::maximumSsidLength);
    networks[index].ssid[WifiNetworkInfo::maximumSsidLength] = '\0';
    networks[index].rssi = WiFi.RSSI(static_cast<int>(index));
    networks[index].encrypted =
        WiFi.encryptionType(static_cast<int>(index)) != WIFI_AUTH_OPEN;
  }

  WiFi.scanDelete();
  return result;
}

#endif

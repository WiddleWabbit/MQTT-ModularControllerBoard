#include "Esp32Wifi.h"

#include <WiFi.h>

// ========== Public API ==========

/**
 * Starts an ESP32 station connection.
 *
 * @param ssid Network name.
 * @param password Network password.
 * @return Nothing.
 */
void Esp32Wifi::begin(const char* ssid, const char* password)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

/**
 * Reads the ESP32 station state.
 *
 * @return Abstract WiFi link state.
 */
WifiLinkState Esp32Wifi::status() const
{
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED)
  {
    return WifiLinkState::Connected;
  }
  if (status == WL_IDLE_STATUS || status == WL_NO_SHIELD)
  {
    return WifiLinkState::Connecting;
  }
  return WifiLinkState::Disconnected;
}

/**
 * Disconnects the ESP32 station.
 *
 * @return Nothing.
 */
void Esp32Wifi::disconnect()
{
  WiFi.disconnect();
}

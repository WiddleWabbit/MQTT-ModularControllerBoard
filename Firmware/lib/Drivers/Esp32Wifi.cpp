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
 * Reads the ESP32 station signal strength.
 *
 * @return Received signal strength in dBm.
 */
int32_t Esp32Wifi::rssi() const
{
  return WiFi.RSSI();
}

/**
 * Reads the ESP32 station address.
 *
 * @return Assigned IPv4 address, or 0.0.0.0 when none is assigned.
 */
Ipv4Address Esp32Wifi::localIp() const
{
  const IPAddress address = WiFi.localIP();
  Ipv4Address ip{{address[0], address[1], address[2], address[3]}};
  return ip;
}

/**
 * Stores the DHCP hostname committed on the next station start.
 *
 * @param hostname Non-empty hostname.
 * @return Nothing.
 */
void Esp32Wifi::setHostname(const char* hostname)
{
  if (hostname != nullptr && hostname[0] != '\0')
  {
    WiFi.setHostname(hostname);
  }
}

/**
 * Stops station mode so the next station start commits the hostname.
 *
 * Arduino-ESP32 2.0 commits the DHCP hostname only when station mode changes.
 *
 * @return Nothing.
 */
void Esp32Wifi::resetStationMode()
{
  WiFi.mode(WIFI_OFF);
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

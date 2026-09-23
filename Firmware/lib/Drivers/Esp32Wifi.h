#pragma once

#include "IWifi.h"

/**
 * Adapts the ESP32 Arduino WiFi station API to IWifi.
 */
class Esp32Wifi : public IWifi
{
public:
  /**
   * Starts an ESP32 station connection.
   *
   * @param ssid Network name.
   * @param password Network password.
   * @return Nothing.
   */
  void begin(const char* ssid, const char* password) override;

  /**
   * Reads the ESP32 station state.
   *
   * @return Abstract WiFi link state.
   */
  WifiLinkState status() const override;

  /**
   * Reads the ESP32 station signal strength.
   *
   * @return Received signal strength in dBm.
   */
  int32_t rssi() const override;

  /**
   * Reads the ESP32 station address.
   *
   * @return Assigned IPv4 address, or 0.0.0.0 when none is assigned.
   */
  Ipv4Address localIp() const override;

  /**
   * Stores the DHCP hostname committed on the next station start.
   *
   * @param hostname Non-empty hostname.
   * @return Nothing.
   */
  void setHostname(const char* hostname) override;

  /**
   * Stops station mode so the next station start commits the hostname.
   *
   * @return Nothing.
   */
  void resetStationMode() override;

  /**
   * Disconnects the ESP32 station.
   *
   * @return Nothing.
   */
  void disconnect() override;
};

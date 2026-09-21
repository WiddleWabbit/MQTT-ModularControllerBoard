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
   * Disconnects the ESP32 station.
   *
   * @return Nothing.
   */
  void disconnect() override;
};

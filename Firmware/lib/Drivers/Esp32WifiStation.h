#pragma once

#include "IWifiStation.h"

class Esp32WifiStation : public IWifiStation {
public:
  /**
   * Disconnects the ESP32 station connection.
   *
   * @return Nothing.
   */
  void disconnect() override;

  /**
   * Starts an ESP32 station connection attempt.
   *
   * @param ssid Network name to connect to.
   * @param password Network password, which may be empty.
   * @return Nothing.
   */
  void begin(const char* ssid, const char* password) override;

  /**
   * Reports the ESP32 station connection state.
   *
   * @return True when connected; otherwise false.
   */
  bool isConnected() const override;
};

#pragma once

#include <cstdint>

enum class WifiLinkState : uint8_t
{
  Disconnected,
  Connecting,
  Connected
};

/**
 * Abstracts the platform WiFi station implementation.
 */
class IWifi
{
public:
  virtual ~IWifi() = default;

  /**
   * Starts a nonblocking station connection attempt.
   *
   * @param ssid Network name.
   * @param password Network password.
   * @return Nothing.
   */
  virtual void begin(const char* ssid, const char* password) = 0;

  /**
   * Reads the current station link state.
   *
   * @return Current link state.
   */
  virtual WifiLinkState status() const = 0;

  /**
   * Reads the current station signal strength.
   *
   * @return Received signal strength in dBm.
   */
  virtual int32_t rssi() const = 0;

  /**
   * Stops the current station connection.
   *
   * @return Nothing.
   */
  virtual void disconnect() = 0;
};

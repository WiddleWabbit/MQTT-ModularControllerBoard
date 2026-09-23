#pragma once

#include <cstdint>

enum class WifiLinkState : uint8_t
{
  Disconnected,
  Connecting,
  Connected
};

/**
 * Four-octet IPv4 address. Zero in every octet means no address is assigned.
 */
struct Ipv4Address
{
  uint8_t octets[4];

  /**
   * Reports whether every octet is zero.
   *
   * @return True when the address is 0.0.0.0.
   */
  bool isUnspecified() const
  {
    return octets[0] == 0 && octets[1] == 0 && octets[2] == 0 &&
           octets[3] == 0;
  }
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
   * Reads the current station address.
   *
   * @return Assigned IPv4 address, or 0.0.0.0 when none is assigned.
   */
  virtual Ipv4Address localIp() const = 0;

  /**
   * Stores the DHCP hostname committed on the next station start.
   *
   * @param hostname Non-empty hostname.
   * @return Nothing.
   */
  virtual void setHostname(const char* hostname) = 0;

  /**
   * Stops station mode so a later start commits a new DHCP hostname.
   *
   * @return Nothing.
   */
  virtual void resetStationMode() = 0;

  /**
   * Stops the current station connection.
   *
   * @return Nothing.
   */
  virtual void disconnect() = 0;
};

#pragma once

class IWifiStation {
public:
  /**
   * Releases the interface without owning the concrete station driver.
   *
   * @return Nothing.
   */
  virtual ~IWifiStation() = default;

  /**
   * Disconnects the current station connection.
   *
   * @return Nothing.
   */
  virtual void disconnect() = 0;

  /**
   * Starts a station connection attempt.
   *
   * @param ssid Network name to connect to.
   * @param password Network password, which may be empty.
   * @return Nothing.
   */
  virtual void begin(const char* ssid, const char* password) = 0;

  /**
   * Reports whether the station is currently connected.
   *
   * @return True when connected; otherwise false.
   */
  virtual bool isConnected() const = 0;
};

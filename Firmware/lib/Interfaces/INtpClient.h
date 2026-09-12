#pragma once

class INtpClient {
public:
  virtual ~INtpClient() = default;

  /**
   * Starts an asynchronous synchronization request with an NTP server.
   *
   * @param serverName Host name of the NTP server.
   * @return Nothing.
   */
  virtual void requestSync(const char* serverName) = 0;

  /**
   * Advances the NTP client without blocking.
   *
   * @return Nothing.
   */
  virtual void update() = 0;

  /**
   * Reports whether a valid network time has been received.
   *
   * @return True when synchronized; otherwise false.
   */
  virtual bool isSynchronized() const = 0;
};

#pragma once

#include "INtpClient.h"

class Esp32NtpClient : public INtpClient {
public:
  /**
   * Starts ESP32 SNTP synchronization using the supplied server.
   *
   * @param serverName Host name of the NTP server.
   * @return Nothing.
   */
  void requestSync(const char* serverName) override;

  /**
   * Keeps the ESP32 NTP driver interface consistent; SNTP runs asynchronously.
   *
   * @return Nothing.
   */
  void update() override;

  /**
   * Checks whether the system clock has passed the Unix epoch threshold.
   *
   * @return True when a synchronized time is available.
   */
  bool isSynchronized() const override;
};

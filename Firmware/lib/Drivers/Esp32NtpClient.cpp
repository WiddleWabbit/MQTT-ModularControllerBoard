#include "Esp32NtpClient.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <time.h>

void Esp32NtpClient::requestSync(const char* serverName)
{
  configTime(0, 0, serverName);
}

void Esp32NtpClient::update()
{
}

bool Esp32NtpClient::isSynchronized() const
{
  return time(nullptr) > 100000;
}

#endif

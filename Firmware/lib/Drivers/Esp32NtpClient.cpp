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

time_t Esp32NtpClient::currentTime() const
{
  return time(nullptr);
}

bool Esp32NtpClient::setTimezone(const char* timezone)
{
  if (timezone == nullptr || timezone[0] == '\0') {
    return false;
  }

  setenv("TZ", timezone, 1);
  tzset();
  return true;
}

bool Esp32NtpClient::getLocalTime(struct tm& localTime) const
{
  const time_t now = time(nullptr);
  return localtime_r(&now, &localTime) != nullptr;
}

#endif

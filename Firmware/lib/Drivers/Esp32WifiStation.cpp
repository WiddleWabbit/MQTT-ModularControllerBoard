#include "Esp32WifiStation.h"

#ifdef ARDUINO

#include <WiFi.h>

void Esp32WifiStation::disconnect()
{
  WiFi.disconnect(true);
}

void Esp32WifiStation::begin(const char* ssid, const char* password)
{
  WiFi.begin(ssid, password);
}

bool Esp32WifiStation::isConnected() const
{
  return WiFi.status() == WL_CONNECTED;
}

#endif

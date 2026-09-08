#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef JSONHANDLER_H
#define JSONHANDLER_H

boolean buildSensorJson(float* readings, size_t numReadings, const char* char_time, char* json, size_t jsonSize);

#endif
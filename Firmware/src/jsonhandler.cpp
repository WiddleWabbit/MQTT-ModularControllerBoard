#include <Arduino.h>
#include <ArduinoJson.h>

JsonDocument doc; // NO NEED FOR SIZE? CHECK NO IMPLICATIONS?

boolean buildSensorJson(float* readings, size_t numReadings, const char* charTime, char* json, size_t jsonSize) {

  doc.clear(); // Clear previous contents
  doc["time"] = charTime;
  JsonObject sensors = doc["sensors"].to<JsonObject>();

  for (int i = 0; i < numReadings; i++) {
    sensors[String(i)] = readings[i]; // Add individual readings under their number
  }
  if (serializeJson(doc, json, jsonSize) == 0) { // Serialize to the provided buffer
    return false; // Serialization failed
  }
  
  return true;
}
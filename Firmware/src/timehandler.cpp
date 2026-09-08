#include <Arduino.h>
#include <time.h>
#include <esp_sntp.h> // For SNTP sync status

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800; // UTC+8 for AWST
const int daylightOffset_sec = 0; // No DST in AWST

boolean timeSynced = false; // Time sync state

unsigned long lastSyncAttempt = 0;
const unsigned long syncInterval = 30000; // Retry every 30 seconds if not synced
unsigned long lastTimePrint = 0;
const unsigned long printInterval = 5000; // Print time every 5 seconds

// Function to format the time information in a tminfo into char
// #### Currently MUST BE char[20] ####
void formatTime(struct tm *timeinfo, char* timeStr, size_t len) {
  // snprintf Print in a specified format
  // Formatting: %04d 4 Digits, %02d 2 Digits etc.
  snprintf(timeStr, len, "%04d-%02d-%02d %02d:%02d:%02d", 
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

// Initiate the ESP32's ConfigTime to configure NTP, Start attempting to sync
void initNTP() {
  Serial.println("Preparing to sync time with NTP Server");
  Serial.print("Sync Interval is: ");
  Serial.println(sntp_get_sync_interval()); // Try to get the sync interval
  Serial.println("Beginning Sync");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Try to initiate a sync
}

// Save the current time into an char array
boolean getTime(char* timeStr, size_t len) {
  if (timeSynced) {

      time_t now = time(nullptr); // Get the current time from the system, I.e. hour, second etc.
      struct tm timeinfo; // Structure for breaking down a time into each component.
      localtime_r(&now, &timeinfo); // Convert time_t into tm structure with daylight savings and GMT offset
      formatTime(&timeinfo, timeStr, len); // Reformat the time into provided timeStr
      return true;

  } else {
    return false;
  }
}

// Check NTP Status, print status to Serial
void checkNTP() {

  if (!timeSynced && millis() - lastSyncAttempt >= syncInterval) { // Non-blocking time sync check
    lastSyncAttempt = millis();
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) { // Check if sync is completed

      timeSynced = true; // Mark time as synced
      time_t now = time(nullptr); // Get the current time from the system, I.e. hour, second etc.

      struct tm timeinfo; // Structure for breaking down a time into each component.
      char time[20]; // Fixed length characters for time
      localtime_r(&now, &timeinfo); // Convert time_t into tm structure with daylight savings and GMT offset
      
      Serial.print("Time synced: ");
      formatTime(&timeinfo, time, sizeof(time)); // Format from tm to char
      Serial.println(time);

    } else {
      Serial.println("Time not currently synced.");
    }
  }

    // Periodically print time if synced
  if (timeSynced && millis() - lastTimePrint >= printInterval) {

    lastTimePrint = millis();
    time_t now = time(nullptr); // Get the current time from the system, I.e. hour, second etc.
    struct tm timeinfo; // Structure for breaking down a time into each component.
    char time[20]; // Fixed length characters for time

    localtime_r(&now, &timeinfo); // Convert time_t into tm structure with daylight savings and GMT offset

    Serial.print("Current Time: ");
    formatTime(&timeinfo, time, sizeof(time)); // Format from tm to char
    Serial.println(time);

  }
}
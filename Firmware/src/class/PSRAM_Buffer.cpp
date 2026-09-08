#include <Arduino.h>
#include "Class\PSRAM_Buffer.h"
#include "mqtthandler.h"

// Constructor implementation
PSRAM_BUFFER::PSRAM_BUFFER(unsigned long max_size) : max_size_(max_size), write_index_(0), read_index_(0) 
{
  // Assigns PSRAM of max_size_ multiplied by the size of the JsonSensorData
  // (JsonSensorData*) type cast so that returned pointer to beginning is of type JsonSensorData
  // ps_malloc assigns PSRAM
  // (max_size_ * sizeof(JsonSensorData)) Specifies the size of PSRAM Allocation, 
  //  max size (number) multiplied by the size of the JsonSensorObject
  buffer_ = (JsonSensorData*)ps_malloc(max_size_ * sizeof(JsonSensorData));
  // ps_malloc returns a pointer to the beginning of the allocated psram
  // If null then allocation hsa failed
  if (!buffer_) {
    Serial.println("PSRAM allocation failed");
  }
}

// Function to add an object to the PSRAM Buffer
boolean PSRAM_BUFFER::addObject(const JsonSensorData& obj) {
  if (!buffer_) return false;
  if (size() >= max_size_) {
    read_index_ = (read_index_ + 1) % max_size_; // Overwrite oldest
    Serial.println("Buffer full, overwriting oldest data");
  }
  buffer_[write_index_] = obj;
  write_index_ = (write_index_ + 1) % max_size_;
  return true;
}

// Send a single buffered object (oldest) and clear it from the buffer
boolean PSRAM_BUFFER::sendSingleAndClear() {
  if (!buffer_ || size() == 0) return true; // Buffer is empty, nothing to do

  char json[192];
  memcpy(json, buffer_[read_index_].json, sizeof(json)); // Memory to memory copy

  Serial.print("Preparing to send: ");
  Serial.println(json);

  if (!mqttClient.publish("/sensors/depth/tanks", json, sizeof(json))) { // If publish fails

    Serial.println("MQTT publish failed to publish: ");
    Serial.println(json);
    return false;
    
  } else { // Successfull publish

    Serial.println("Sent succesfully");
    read_index_ = (read_index_ + 1) % max_size_;
    return true;

  }
}

// Send all the objects in the buffer and clear them all. If unsuccessfull don't clear any.
boolean PSRAM_BUFFER::sendAndClear() {
  if (!buffer_ || size() == 0) return true; // Buffer is empty, nothing to do

  bool success = true;
  unsigned long sent_count = 0;

  for (unsigned long i = read_index_; i != write_index_; i = (i + 1) % max_size_) {

    char json[192];
    memcpy(json, buffer_[i].json, sizeof(json)); // Memory to memory copy

    // Serial.print("Preparing to send: ");
    // Serial.println(json);

    if (!mqttClient.publish("/sensors/depth/tanks", json, sizeof(json))) { // If publish fails

      Serial.println("MQTT publish failed to publish: ");
      Serial.println(json);
      success = false;
      break;
      
    } else { // Successfull publish

      Serial.println("Sent succesfully");

    }
    sent_count++;
  }

  if (success) {
    read_index_ = write_index_;
    Serial.printf("Sent and cleared %u objects\n", sent_count);
  }
  return success;
}

unsigned long PSRAM_BUFFER::size() const {
  return (write_index_ + max_size_ - read_index_) % max_size_;
}
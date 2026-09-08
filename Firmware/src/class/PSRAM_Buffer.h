#include <Arduino.h>

#ifndef PSRAM_BUFFER_H
#define PSRAM_BUFFER_H

// MODIFY THE SIZE OF THE JSON VARIABLE AND THE BUFFER ONE FROM MAIN TO USE THE SAME SIZE VARIABLE SO ONLY SPECIFIED ONCE I.E. json[SIZEHERE]

struct JsonSensorData {
    char json[192]; // JSON string sized for payload
};

class PSRAM_BUFFER {

  protected:
    JsonSensorData* buffer_;
    unsigned long max_size_;
    unsigned long write_index_;
    unsigned long read_index_;

  public:
    // Define Constructor
    PSRAM_BUFFER(unsigned long max_size);
    // Define Methods
    boolean sendAndClear();
    boolean sendSingleAndClear();
    boolean addObject(const JsonSensorData& obj);
    unsigned long size() const;

  ~PSRAM_BUFFER() {
    if (buffer_) {
      free(buffer_);
    }
  }

};

#endif
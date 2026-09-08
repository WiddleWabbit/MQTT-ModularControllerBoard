#include <Arduino.h>
#include <time.h>

#ifndef TIMEHANDLER_H
#define TIMEHANDLER_H

extern boolean timeSynced;

void formatTime(struct tm *timeinfo, char* timeStr, size_t len);
void initNTP();
void checkNTP();
boolean getTime(char* timeStr, size_t len);

#endif
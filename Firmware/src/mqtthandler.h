#include <Arduino.h>
#include <PubSubClient.h>

#ifndef MQTTHANDLER_H
#define MQTTHANDLER_H

extern PubSubClient mqttClient;

boolean initMQTT();
void checkMQTT();
void callback(char *topic, byte *payload, unsigned int length);

#endif
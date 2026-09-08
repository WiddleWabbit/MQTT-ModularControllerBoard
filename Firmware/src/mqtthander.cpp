#include <Arduino.h> // Ardunio Library
#include <WiFi.h> // Wifi Library
#include <PubSubClient.h> // Library to handle MQTT
#include "mqtthandler.h"

// MQTT Broker
const char *mqtt_broker = "192.168.88.220";
const char *mqtt_clientname = "watering_controller";
const char *topic = "/watering/";
const char *mqtt_username = "wateringController";
const char *mqtt_password = "";
const int mqtt_port = 1883;

const int maxPacketSize = 512;

long lastReconnectAttempt = 0;

// Create a wifi client and set the MQTT client to use it
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Set MQTT Settings and Attempt to Connect and Subscribe
boolean initMQTT() {
  
  mqttClient.setServer(mqtt_broker, mqtt_port); // Set the MQTT Broker Server
  mqttClient.setCallback(callback); // Set the callback to run on message recieved

  if (mqttClient.connect(mqtt_clientname, mqtt_username, mqtt_password)) { // Attempt to connect

    lastReconnectAttempt = 0; // Reset reconnection timer

    mqttClient.setBufferSize(maxPacketSize);
    Serial.print("Set max buffer size to: ");
    Serial.println(maxPacketSize);

    Serial.print("Max buffer size: ");
    Serial.println(mqttClient.getBufferSize());

    mqttClient.subscribe(topic); // Resubscribe to topics
    Serial.print("Subscribed to topic: ");
    Serial.println(topic);

  }
  return mqttClient.connected();
}

// Check MQTT Status
void checkMQTT() {
  
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (initMQTT()) { // Attempt to reconnect
        lastReconnectAttempt = 0;
      }
    }
  }
}

// Callback for receiving a message on subcribed topics
void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
}
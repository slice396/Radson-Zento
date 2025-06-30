#ifndef MQTT_H
#define MQTT_H

#include <WiFi.h>
#include <PubSubClient.h>

// Externe MQTT-client
extern PubSubClient mqttClient;

// Initialisatie en basisverbinding
void setupMQTT();                              // Verbindt met de MQTT-broker
void loopMQTT(bool* relaisStatus, unsigned long* lastOnTimes, unsigned long* lastOffTimes, int relaisCount);  // Houdt de verbinding in stand en publiceert periodiek

// Publicatie
void publishRelaisStatus(bool* relaisStatus, unsigned long* lastOnTimes, unsigned long* lastOffTimes, int relaisCount); // Stuurt relaisstatus naar MQTT
void publishBufferTemperature(float bufferTemperature); // Stuurt buffertemperatuur naar MQTT
void sendRuntimeToMQTT(int pumpIndex, unsigned long runtime); // Stuurt individuele runtime door
void updateStarttime();                          // Stuurt opstarttijd door

// Ophalen
void getAllRuntimes(unsigned long* runtimes);    // Haalt runtimes op voor alle pompen
String GetMode(); // Haalt modus op (Verwarmen / Koelen / Niks)

// Utilities
bool topicExists(const char* topic);             // Controleert of een topic actief is
void mqttCallback(char* topic, byte* payload, unsigned int length); // Standaard callback functie

#endif

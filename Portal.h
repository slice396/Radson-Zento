#ifndef PORTAL_H
#define PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
extern WebServer server; // Gebruik de gedeelde WebServer-instantie
extern String mqttLog;
extern String rebootReason;


// Externe arrays voor relaisstatus en tijdstempels
extern bool relayStatus[6];
extern unsigned long lastOnTimes[6];
extern unsigned long lastOffTimes[6];

// Functie om relaisstatus naar de portal te publiceren
void publishRelayStatus(bool* relaisStatus, unsigned long* lastOnTimes, unsigned long* lastOffTimes, int relaisCount);

// Functie om de webportal in te stellen
void setupPortal(const char* hostname);

// Externe functie voor tijdsweergave
String getFormattedTime();

#endif // PORTAL_H
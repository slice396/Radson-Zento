#ifndef PUMPMASTER_H
#define PUMPMASTER_H

#include "Debug.h"
#include <Arduino.h>
#include <ArduinoJson.h> // Toevoegen voor JSON-functionaliteit

class PumpMaster {
public:
    // Constructor
    PumpMaster();

    // Update functie om huidige en doeltemperaturen door te geven
    void update(float currentTemp, float targetTemp, bool heating, float hysteresis);
    

    // Regeling voor de pompen
    void regulatePumps(bool heating, float hysteresis);

    // Update runtimes van MQTT en geef door aan MQTT.cpp
    void updateRuntimeFromMQTT();

    // Status van een specifieke pomp ophalen
    bool getPumpStatus(int pumpIndex);

    // Verkrijg laatste inschakeltijd van een pomp
    unsigned long getLastOnTime(int pumpIndex);

    // Verkrijg laatste uitschakeltijd van een pomp
    unsigned long getLastOffTime(int pumpIndex);

    // Geforceerd een pomp uitschakelen
    void forcePumpOff(int pumpIndex);

    // Alle pompen uitschakelen
    void shutdownAllPumps();

private:
    // Buffertemperaturen
    float currentBufferTemp;
    float targetBufferTemp;

    // Pompstatus
    bool pumpStatus[3];

    // Tijdregistratie
    unsigned long lastOnTime[3];
    unsigned long lastOffTime[3];
    unsigned long lastPumpChangeTime;
    unsigned long lastTempCheckTime;

    // Laatst gemeten temperatuur
    float lastMeasuredTemp;

    // Laatst bekende runtimes en update tijdstip
    unsigned long savedRuntime[3];
    unsigned long lastRuntimeUpdate;
};

#endif // PUMPMASTER_H

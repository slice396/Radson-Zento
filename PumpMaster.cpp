#include "PumpMaster.h"
#include "MQTT.h" // Zorg ervoor dat MQTT.cpp is geïmporteerd voor getRuntime en publiceren
#include "Debug.h"

// Globale wachttijden (in milliseconden)
const unsigned long NORMAL_ON_TIME = 15 * 60 * 1000;
const unsigned long NORMAL_OFF_TIME = 15 * 60 * 1000;
const unsigned long NORMAL_CHANGE_TIME = 30 * 60 * 1000;
const float TEMP_HYSTERESIS = 5.0;
const unsigned long RUNTIME_UPDATE_INTERVAL = 10 * 60 * 1000;

// Draaiuren opslag
unsigned long savedRuntime[3] = {0, 0, 0};
unsigned long lastRuntimeUpdate = 0;

bool PumpMaster::getPumpStatus(int pumpIndex) {
    if (pumpIndex >= 0 && pumpIndex < 3) {
        return pumpStatus[pumpIndex];
    } else {
        return false;
    }
}

void PumpMaster::updateRuntimeFromMQTT() {
    unsigned long runtimes[3] = {0, 0, 0};
    getAllRuntimes(runtimes); // Haal alle runtimes in één keer op

    for (int i = 0; i < 3; i++) {
        if (runtimes[i] != 0 && runtimes[i] != -1) {
            savedRuntime[i] = runtimes[i];
        }

        sendRuntimeToMQTT(i, savedRuntime[i]);
    }
}

// Constructor
PumpMaster::PumpMaster() {
    for (int i = 0; i < 3; i++) {
        pumpStatus[i] = false;
        lastOnTime[i] = 0;
        lastOffTime[i] = 0;
    }
    currentBufferTemp = 0.0;
    targetBufferTemp = 0.0;
    lastPumpChangeTime = 0;
    lastRuntimeUpdate = millis();
    updateRuntimeFromMQTT(); // Directe initiële update
}

// Update de buffertemperaturen
void PumpMaster::update(float currentTemp, float targetTemp, bool heating, float hysteresis) {
    currentBufferTemp = currentTemp;
    targetBufferTemp = targetTemp;

    unsigned long currentTime = millis();

    if (currentTime - lastRuntimeUpdate >= RUNTIME_UPDATE_INTERVAL) {
        updateRuntimeFromMQTT();
        lastRuntimeUpdate = currentTime;
    }

    regulatePumps(heating, hysteresis);
}


void PumpMaster::shutdownAllPumps() {
    for (int i = 0; i < 3; i++) pumpStatus[i] = false;
}

void PumpMaster::forcePumpOff(int pumpIndex) {
    pumpStatus[pumpIndex] = false;
    // eventueel logica toevoegen voor handmatige override
}

// Regeling voor de pompen
// Update runtimes van MQTT en geef door aan MQTT.cpp
void PumpMaster::regulatePumps(bool heating, float hysteresis) {
    unsigned long currentTime = millis();

    if (currentTime - lastPumpChangeTime < NORMAL_CHANGE_TIME) {
        return;
    }

    // Verwarmen of koelen – logica keert om
    if (heating) {
        // **Afschakelen bij bereiken doel + hysteresis**
        if (currentBufferTemp >= targetBufferTemp + hysteresis) {
            int maxRuntimeIndex = -1;
            unsigned long maxRuntime = 0;

            for (int i = 0; i < 3; i++) {
                if (pumpStatus[i] && savedRuntime[i] > maxRuntime) {
                    maxRuntime = savedRuntime[i];
                    maxRuntimeIndex = i;
                }
            }

            if (maxRuntimeIndex != -1) {
                pumpStatus[maxRuntimeIndex] = false;
                lastOffTime[maxRuntimeIndex] = currentTime;
                lastPumpChangeTime = currentTime;
                debugPrint("Pomp " + String(maxRuntimeIndex + 1) + " uitgeschakeld (verwarmen).");
            }
            return;
        }

        // **Inschakelen bij onder doel - hysteresis**
        if (currentBufferTemp <= targetBufferTemp - hysteresis) {
            int minRuntimeIndex = -1;
            unsigned long minRuntime = ULONG_MAX;

            for (int i = 0; i < 3; i++) {
                if (!pumpStatus[i] && savedRuntime[i] < minRuntime &&
                    currentTime - lastOffTime[i] >= NORMAL_OFF_TIME) {
                    minRuntime = savedRuntime[i];
                    minRuntimeIndex = i;
                }
            }

            if (minRuntimeIndex != -1) {
                pumpStatus[minRuntimeIndex] = true;
                lastOnTime[minRuntimeIndex] = currentTime;
                lastPumpChangeTime = currentTime;
                debugPrint("Pomp " + String(minRuntimeIndex + 1) + " ingeschakeld (verwarmen).");
            }
        }

    } else {
        // Koelen – omgekeerde logica
        if (currentBufferTemp <= targetBufferTemp - hysteresis) {
            int maxRuntimeIndex = -1;
            unsigned long maxRuntime = 0;

            for (int i = 0; i < 3; i++) {
                if (pumpStatus[i] && savedRuntime[i] > maxRuntime) {
                    maxRuntime = savedRuntime[i];
                    maxRuntimeIndex = i;
                }
            }

            if (maxRuntimeIndex != -1) {
                pumpStatus[maxRuntimeIndex] = false;
                lastOffTime[maxRuntimeIndex] = currentTime;
                lastPumpChangeTime = currentTime;
                debugPrint("Pomp " + String(maxRuntimeIndex + 1) + " uitgeschakeld (koelen).");
            }
            return;
        }

        if (currentBufferTemp >= targetBufferTemp + hysteresis) {
            int minRuntimeIndex = -1;
            unsigned long minRuntime = ULONG_MAX;

            for (int i = 0; i < 3; i++) {
                if (!pumpStatus[i] && savedRuntime[i] < minRuntime &&
                    currentTime - lastOffTime[i] >= NORMAL_OFF_TIME) {
                    minRuntime = savedRuntime[i];
                    minRuntimeIndex = i;
                }
            }

            if (minRuntimeIndex != -1) {
                pumpStatus[minRuntimeIndex] = true;
                lastOnTime[minRuntimeIndex] = currentTime;
                lastPumpChangeTime = currentTime;
                debugPrint("Pomp " + String(minRuntimeIndex + 1) + " ingeschakeld (koelen).");
            }
        }
    }
}


// Verkrijg laatste uitschakeltijd van een pomp
unsigned long PumpMaster::getLastOffTime(int pumpIndex) {
    if (pumpIndex >= 0 && pumpIndex < 3) {
        return lastOffTime[pumpIndex];
    } else {
        return 0;
    }
}

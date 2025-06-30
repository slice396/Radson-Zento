// Toevoeging van koelmogelijkheid. De waarde koelen/verwarmen haalt de regelaar uit MQTT.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <ShiftRegister74HC595_NonTemplate.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "esp_system.h"
#include "Debug.h"
#include "PumpMaster.h" // Regelt de logica voor het verwarmen van de buffervaten.
#include "Portal.h" // Regelt dat de informatie met de gebruikers wordt gedeeld. Als gebruiker kan je inloggen via verwarming.local
#include "MQTT.h" // Regelt dat er een MQTT tabel word gemaakt. Deze tabel word gedeeld met Portal.h en PumpMaster.h en aangevuld door de 3 warmtepompen.

bool relayStatus[6] = {false, false, false, false, false, false}; // Alle relays standaard uit
unsigned long lastOnTimes[6] = {0, 0, 0, 0, 0, 0}; // Laatste inschakeltijden voor alle relais
unsigned long lastOffTimes[6] = {0, 0, 0, 0, 0, 0}; // Laatste uitschakeltijden voor alle relais
String mqttLog = ""; // Log voor MQTT-berichten

bool pumpStatus[3] = {false, false, false}; // Alle pompen starten uit
String laatsteMode = "Verwarmen"; // Standaard starten in Verwarmen

// Externe configuratie
#define DATA_PIN 7
#define CLOCK_PIN 5
#define LATCH_PIN 6
#define ENABLE_PIN 4
#define ONE_WIRE_BUS 9
#define OUTDOOR_TEMP_SENSOR_INDEX 1
#define BUFFER_TEMP_SENSOR_INDEX 0

const char* hostname = "verwarming";
const char* weatherEndpoint = "https://api.open-meteo.com/v1/forecast?latitude=51.9125&longitude=4.3417&current_weather=true";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// Bij problemen zijn onderstaande variabelen nodig
unsigned long invalidTempStartTime = 0;
const unsigned long MAX_INVALID_TEMP_DURATION = 30UL * 60UL * 1000UL; // 30 minuten
bool alarmTriggered = false;


OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);
ShiftRegister74HC595_NonTemplate* control;
PumpMaster pumpMaster;

float bufferTemperature = 0.0;
float outdoorTemperatureOnline = 0.0;
bool debugMode = false;

void turnRelaysOff() {
    for (int i = 0; i < 8; i++) { // We hebben 8 kanalen, 6 voor een relais en 2 voor een led.
        control->set(i, LOW);
        control->set(6, HIGH);  // CH7 aan als de module start
    }
}

String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Failed to obtain time";
    }
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    debugPrint("Begonnen met de serial communicatie");
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, HIGH);
    control = new ShiftRegister74HC595_NonTemplate(8, DATA_PIN, CLOCK_PIN, LATCH_PIN); // Uitbreiden naar 8 outputs
    turnRelaysOff();
    digitalWrite(ENABLE_PIN, LOW);
    sensors.begin();

    WiFiManager wifiManager;
    wifiManager.setHostname(hostname);

    if (wifiManager.autoConnect("LilyGO-Relay")) {
        control->set(7, LOW);  // CH8 uit als WiFi werkt
        debugPrint("WiFi verbonden");
    } else {
        control->set(7, HIGH); // CH8 aan bij falende WiFi-verbinding
        debugPrint("WiFi-verbinding mislukt");
    }

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    setupMQTT();

    // Start de portal via Portal.cpp
    setupPortal(hostname);
    control->set(6, LOW);  // CH7 uit als setup is afgerond

    // Voeg de starttijd toe aan MQTT.
    updateStarttime();

    esp_reset_reason_t reason = esp_reset_reason();

    switch (reason) {
        case ESP_RST_UNKNOWN: rebootReason = "Onbekend"; break;
        case ESP_RST_POWERON: rebootReason = "Power-on Reset"; break;
        case ESP_RST_EXT:     rebootReason = "Externe Reset"; break;
        case ESP_RST_SW:      rebootReason = "Software Reset"; break;
        case ESP_RST_PANIC:   rebootReason = "Crash (Panic)"; break;
        case ESP_RST_INT_WDT: rebootReason = "Interrupt Watchdog"; break;
        case ESP_RST_TASK_WDT: rebootReason = "Task Watchdog"; break;
        case ESP_RST_WDT:     rebootReason = "Andere Watchdog"; break;
        case ESP_RST_DEEPSLEEP: rebootReason = "Wake uit Deep Sleep"; break;
        case ESP_RST_BROWNOUT: rebootReason = "Brownout Reset (spanning te laag)"; break;
        default: rebootReason = "Onbekende reden (" + String(reason) + ")"; break;
    }

    debugPrint("Laatste reboot reden: " + rebootReason);
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        control->set(7, LOW);
    } else {
        control->set(7, HIGH);
    }

    sensors.requestTemperatures();
    bufferTemperature = sensors.getTempCByIndex(BUFFER_TEMP_SENSOR_INDEX);

    // Mode ophalen via MQTT
    String mode = GetMode();
    if (mode == "Verwarmen" || mode == "Koelen") {
        if (mode != laatsteMode) {
            debugPrint("Modus gewijzigd via MQTT: " + mode);
        }
        laatsteMode = mode;
    }

    // Koelrelais schakelen
    if (laatsteMode == "Koelen") {
        control->set(3, HIGH);      // Koelen AAN
        relayStatus[3] = true;      // Relaystatus ook bijwerken
    } else {
        control->set(3, LOW);       // Koelen UIT
        relayStatus[3] = false;     // Relaystatus ook bijwerken
    }

    // Buffertemperatuur geldig?
    if (bufferTemperature > 0.0) {
        bool heating = (laatsteMode == "Verwarmen");
        float targetTemp = heating ? 30.0 : 14.0;
        float hysteresis = heating ? 5.0 : 1.0;

        pumpMaster.update(bufferTemperature, targetTemp, heating, hysteresis);
        publishBufferTemperature(bufferTemperature);

        invalidTempStartTime = 0;
        alarmTriggered = false;
    } else {
        debugPrint("Buffertemperatuur ongeldig (" + String(bufferTemperature) + " °C)");

        if (invalidTempStartTime == 0) {
            invalidTempStartTime = millis();
            debugPrint("Start met fouttimer voor buffertemperatuur.");
        }

        if (!alarmTriggered && millis() - invalidTempStartTime >= MAX_INVALID_TEMP_DURATION) {
            debugPrint("Buffertemperatuur blijft te lang foutief. Schakel warmtepompen uit en stuur waarschuwing.");

            for (int i = 0; i < 3; i++) {
                pumpMaster.forcePumpOff(i);
            }

            StaticJsonDocument<128> warning;
            warning["type"] = "temperatuur_fout";
            warning["melding"] = "Buffertemperatuur is al 30 minuten ongeldig.";
            warning["actie"] = "Pompen uitgeschakeld.";

            char buffer[128];
            serializeJson(warning, buffer);
            mqttClient.publish("warmtepomp/waarschuwing", buffer, false);

            alarmTriggered = true;
        }
    }

    // Relaisstatus bijwerken
    for (int i = 0; i < 3; i++) {
        bool currentStatus = pumpMaster.getPumpStatus(i);
        if (currentStatus != relayStatus[i]) {
            relayStatus[i] = currentStatus;
            if (currentStatus) lastOnTimes[i] = millis();
            else lastOffTimes[i] = millis();
        }
        control->set(i, relayStatus[i]);
    }

    loopMQTT(relayStatus, lastOnTimes, lastOffTimes, 6);
    server.handleClient();
    delay(1000);
}

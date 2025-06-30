#define MQTT_MAX_PACKET_SIZE 512

#include "MQTT.h"
#include <ArduinoJson.h>
#include "Debug.h"

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Algemene callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    debugPrint("Bericht ontvangen op topic ");
    debugPrint(topic);
    debugPrint(": ");

    char message[256] = {0};
    length = length < 255 ? length : 255;
    memcpy(message, payload, length);
    message[length] = '\0';
    debugPrint(message);
}

// MQTT initialiseren
void setupMQTT() {
    const char* mqttUsername = "MQTT";
    const char* mqttPassword = "mqtt";

    mqttClient.setServer("homeassistant.local", 1883);
    mqttClient.setCallback(mqttCallback);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000; // max 10 sec proberen
    bool connected = false;

    while (!connected && millis() - startAttemptTime < timeout) {
        debugPrint("Verbinding maken met MQTT...");
        if (mqttClient.connect("ESP32Client", mqttUsername, mqttPassword)) {
            debugPrint("MQTT verbonden!");
            mqttClient.subscribe("warmtepomp/command");
            connected = true;
        } else {
            Serial.print("Verbinding mislukt. Status: ");
            debugPrint(String(mqttClient.state()));
            delay(1000); // korte tussenpauze
        }
    }

    if (!mqttClient.connected()) {
        debugPrint("MQTT-verbinding mislukt. Doorgaan zonder MQTT.");
    }
}

String GetMode() { // Verwarm of koelmodes opvragen.
    if (!mqttClient.connected()) {
        return "Niks"; // Kan niet ophalen
    }

    String mode = "Niks"; // Standaard: niks teruggeven

    bool received = false;

    // Tijdelijke handler voor alleen het mode-topic
    auto modeHandler = [&](char* topic, byte* payload, unsigned int length) {
        if (strcmp(topic, "warmtepomp/mode") == 0) {
            char message[256] = {0};
            length = length < 255 ? length : 255;
            memcpy(message, payload, length);
            message[length] = '\0';

            String incomingMode = String(message);

            if (incomingMode == "Verwarmen" || incomingMode == "Koelen") {
                mode = incomingMode;
                received = true;
            }
        }
    };

    mqttClient.setCallback(modeHandler);

    mqttClient.subscribe("warmtepomp/mode");

    unsigned long startTime = millis();
    while (!received && millis() - startTime < 2000) { // Maximaal 2 seconden wachten
        mqttClient.loop();
    }

    mqttClient.unsubscribe("warmtepomp/mode");

    // Herstel oorspronkelijke callback
    mqttClient.setCallback(mqttCallback);

    return mode;
}

// Test of topic actief is
bool topicExists(const char* topic) {
    if (!mqttClient.connected()) return false;

    mqttClient.publish(topic, "test", true);
    delay(100);
    mqttClient.publish(topic, "", true);
    return true;
}

// Starttijd publiceren
void updateStarttime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        debugPrint("Kon starttijd niet verkrijgen");
        return;
    }

    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);

    if (mqttClient.connected()) {
        if (!mqttClient.publish("warmtepomp/starttijd", timeStringBuff, true)) {
            debugPrint("Publicatie starttijd mislukt.");
        } else {
            debugPrint("Starttijd gepubliceerd op MQTT: " + String(timeStringBuff));
        }
    }
}

// Runtime data ophalen
void getAllRuntimes(unsigned long* runtimes) {
    if (!mqttClient.connected()) {
        for (int i = 0; i < 3; i++) runtimes[i] = 0;
        return;
    }

    bool received[3] = {false, false, false};

    // Tijdelijke topic handler
    auto runtimeHandler = [&](char* topic, byte* payload, unsigned int length) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) return;

        for (int i = 0; i < 3; i++) {
            char expectedTopic[50];
            snprintf(expectedTopic, sizeof(expectedTopic), "warmtepomp/pump/%d/status", i);
            if (strcmp(topic, expectedTopic) == 0 && doc.containsKey("run_time")) {
                runtimes[i] = doc["run_time"].as<unsigned long>();
                received[i] = true;
            }
        }
    };

    mqttClient.setCallback(runtimeHandler);

    for (int i = 0; i < 3; i++) {
        char topic[50];
        snprintf(topic, sizeof(topic), "warmtepomp/pump/%d/status", i);
        mqttClient.subscribe(topic);
    }

    unsigned long startTime = millis();
    while ((!received[0] || !received[1] || !received[2]) && millis() - startTime < 5000) {
        mqttClient.loop();
    }

    for (int i = 0; i < 3; i++) {
        char topic[50];
        snprintf(topic, sizeof(topic), "warmtepomp/pump/%d/status", i);
        mqttClient.unsubscribe(topic);
    }

    // Herstel oorspronkelijke callback
    mqttClient.setCallback(mqttCallback);
}

// Publiceer draaitijd
void sendRuntimeToMQTT(int pumpIndex, unsigned long runtime) {
    if (!mqttClient.connected()) return;

    char topic[50];
    snprintf(topic, sizeof(topic), "warmtepomp/runtime/%d", pumpIndex);

    StaticJsonDocument<64> doc;
    doc["runtime"] = runtime;
    char buffer[64];
    serializeJson(doc, buffer);

    if (!mqttClient.publish(topic, buffer, true)) {
        debugPrint("Publicatie runtime mislukt voor pomp " + String(pumpIndex));
    }
}

// Publiceer buffertemperatuur
void publishBufferTemperature(float bufferTemperature) {
    if (!mqttClient.connected()) return;

    StaticJsonDocument<64> doc;
    doc["buffer_temperature"] = bufferTemperature;
    char buffer[64];
    serializeJson(doc, buffer);

    if (!mqttClient.publish("warmtepomp/buffer_temperature", buffer, true)) {
        debugPrint("Publicatie buffer temperatuur mislukt");
    }
}

// Publiceer relaisstatussen
void publishRelaisStatus(bool* relaisStatus, unsigned long* lastOnTimes, unsigned long* lastOffTimes, int relaisCount) {
    if (!mqttClient.connected()) return;

    for (int i = 0; i < relaisCount; i++) {
        StaticJsonDocument<128> doc;
        doc["status"] = relaisStatus[i] ? "ON" : "OFF";
        doc["last_on"] = lastOnTimes[i];
        doc["last_off"] = lastOffTimes[i];

        char buffer[128];
        serializeJson(doc, buffer);

        char topic[50];
        snprintf(topic, sizeof(topic), "warmtepomp/relay/%d/status", i);

        if (!mqttClient.publish(topic, buffer, true)) {
            debugPrint("Publicatie relaisstatus mislukt voor relais " + String(i));
        }
    }
}

// MQTT-loop
void loopMQTT(bool* relaisStatus, unsigned long* lastOnTimes, unsigned long* lastOffTimes, int relaisCount) {
    if (!mqttClient.connected()) {
        setupMQTT(); // Probeer te reconnecten
    }
    mqttClient.loop();

    static unsigned long lastPublishTime = 0;
    if (millis() - lastPublishTime > 10000) {
        publishRelaisStatus(relaisStatus, lastOnTimes, lastOffTimes, relaisCount);
        lastPublishTime = millis();
    }
}

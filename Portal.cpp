#include "Portal.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "PumpMaster.h"
#include <Update.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#include "Debug.h"

// Externe variabelen gedeclareerd in Warmtepompregelaar.ino
extern WebServer server;
extern DallasTemperature sensors;
extern float bufferTemperature;
extern String debugLog;
extern bool relayStatus[6];
extern unsigned long lastOnTimes[6]; // Laatste inschakeltijden
extern unsigned long lastOffTimes[6]; // Laatste uitschakeltijden
extern String mqttLog; // Externe MQTT-log
extern PubSubClient mqttClient;
extern String rebootReason;
extern float outdoorTemperatureOnline;
bool manualMode = false; // Automatisch of handmatig schakelen

String formatTime(unsigned long timestamp) {
    if (timestamp == 0) return "N/A"; // Geen tijd beschikbaar
    time_t rawTime = timestamp; // Direct naar seconden
    struct tm* timeInfo = localtime(&rawTime);
    char buffer[10];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeInfo);
    return String(buffer);
}

void setupPortal(const char* hostname) {
    if (!MDNS.begin(hostname)) {
        debugPrint("Error setting up mDNS responder!");
    } else {
        debugPrint(String("mDNS responder started. Visit: http://") + hostname + ".local");
        MDNS.addService("http", "tcp", 80);
    }

    debugPrint("WebServer gestart op IP: " + WiFi.localIP().toString());

    server.on("/", HTTP_GET, []() {
        sensors.requestTemperatures();
        String page = "<html><head>";
        if (!server.hasArg("updating")) { // Alleen refreshtimer als er geen update plaatsvindt
            page += "<meta http-equiv=\"refresh\" content=\"60\">"; // 1 minuut refresh
        }
        page += "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css\">";
        page += "</head><body class='bg-light'>";
        page += "<div class='container mt-5'>";
        page += "<h1 class='text-center mb-4'>LilyGO Relay Portal V3</h1>";

        // Status en modus
        page += "<div class='card mb-3'><div class='card-body'>";
        page += "<h5 class='card-title'>Mode</h5>";
        page += "<form method='POST' action='/mode'>";
        page += "<label><input type='radio' name='mode' value='auto' " + String(!manualMode ? "checked" : "") + "> Automatisch</label><br>";
        page += "<label><input type='radio' name='mode' value='manual' " + String(manualMode ? "checked" : "") + "> Handmatig</label><br>";
        page += "<button type='submit' class='btn btn-primary mt-2'>Opslaan</button>";
        page += "</form>";
        page += "</div></div>";

        // Relais status
        page += "<div class='card'><div class='card-body'>";
        page += "<h5 class='card-title'>Relais Status</h5><ul class='list-group'>";
        for (int i = 0; i < 6; i++) {
            page += "<li class='list-group-item d-flex justify-content-between align-items-center'>Relay " + String(i + 1);
            page += (relayStatus[i] ? "<span class='badge bg-success'>On</span>" : "<span class='badge bg-danger'>Off</span>");
            page += "<br>Last On: " + formatTime(lastOnTimes[i]);
            page += "<br>Last Off: " + formatTime(lastOffTimes[i]);
            if (manualMode) {
                page += "<form method='POST' action='/toggle?relay=" + String(i) + "'>";
                page += "<button type='submit' class='btn btn-sm btn-secondary mt-2'>" + String(relayStatus[i] ? "Uitzetten" : "Aanzetten") + "</button>";
                page += "</form>";
            }
            page += "</li>";
        }
        page += "</ul></div></div>";

        // MQTT Info
        page += "<div class='card mt-3'><div class='card-body'>";
        page += "<h5 class='card-title'>MQTT Info</h5><table class='table'>";
        page += "<thead><tr><th>Topic</th><th>Payload</th></tr></thead><tbody>";

        if (mqttClient.connected()) {
            mqttClient.loop();
            page += mqttLog;
        } else {
            page += "<tr><td colspan='2'>Niet verbonden met MQTT</td></tr>";
        }

        page += "</tbody></table></div></div>";

        // Outdoor Temperature
        page += "<div class='card mt-3'><div class='card-body'>";
        page += "<h5 class='card-title'>Buitentemperatuur</h5>";
        page += "<p>" + String(outdoorTemperatureOnline) + "&deg;C</p>";
        page += "</div></div>";

        // Firmware Update
        page += "<div class='card mt-3'><div class='card-body'>";
        page += "<h5 class='card-title'>Firmware Update</h5>";
        page += "<form method='POST' action='/update' enctype='multipart/form-data'>";
        page += "<input type='file' name='update' class='form-control mb-3'><br>";
        page += "<button type='submit' class='btn btn-primary'>Update Firmware</button>";
        page += "</form>";
        page += "</div></div>";

        // Debug log
        page += "<div class='card mt-3'><div class='card-body'>";
        page += "<h5 class='card-title'>Debug Log</h5>";
        page += "<div style='font-family:monospace; white-space:pre-wrap; max-height: 300px; overflow-y: auto;'>";
        page += debugLog;
        page += "</div></div></div>";

        // Reboot Reason
        page += "<div class='card mt-3'><div class='card-body'>";
        page += "<h5 class='card-title'>Laatste Reboot Reden</h5>";
        page += "<p>" + rebootReason + "</p>";
        page += "</div></div>";

        page += "</div></body></html>";
        server.send(200, "text/html", page);
    });

    // Verander modus
    server.on("/mode", HTTP_POST, []() {
        if (server.hasArg("mode")) {
            manualMode = server.arg("mode") == "manual";
            debugPrint(String("Mode veranderd naar: ") + (manualMode ? "Handmatig" : "Automatisch"));
        }
        server.sendHeader("Location", "/");
        server.send(303);
    });

    // Toggle relais
    server.on("/toggle", HTTP_POST, []() {
        if (server.hasArg("relay")) {
            int relayIndex = server.arg("relay").toInt();
            if (relayIndex >= 0 && relayIndex < 6) {
                relayStatus[relayIndex] = !relayStatus[relayIndex];
                debugPrint(String("Relay ") + relayIndex + (relayStatus[relayIndex] ? " aangezet" : " uitgezet"));
            }
        }
        server.sendHeader("Location", "/");
        server.send(303);
    });
    // Werkende versie. Niks op aanpassen!
    server.on("/update", HTTP_GET, []() {
        String page = "<html><body class='bg-light'>";
        page += "<div class='container mt-5'>";
        page += "<h1 class='text-center mb-4'>Firmware Update</h1>";
        page += "<form method='POST' action='/update' enctype='multipart/form-data' class='text-center'>";
        page += "<input type='file' name='update' class='form-control mb-3'><br>";
        page += "<input type='submit' value='Update Firmware' class='btn btn-primary'>";
        page += "</form></div></body></html>";
        server.send(200, "text/html", page);
    });

    server.on("/update", HTTP_POST, []() {
        bool shouldReboot = !Update.hasError();
        String message = shouldReboot ? "Update complete. Rebooting..." : "Update failed!";
        debugPrint(message);
        server.send(200, "text/plain", message);
        if (shouldReboot) {
            delay(100);
            ESP.restart();
        }
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            String message = "Update Start: " + upload.filename;
            debugPrint(message);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                String message = "Update Success: " + String(upload.totalSize) + " bytes.";
                debugPrint(message);
            } else {
                Update.printError(Serial);
            }
        }
    });

    server.begin();
    debugPrint("WebServer gestart op IP: " + WiFi.localIP().toString());
}

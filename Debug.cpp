#include "Debug.h"

String debugLog = "";
String rebootReason = "";

void debugPrint(const String& msg) {
    Serial.println(msg); // optioneel, voor als USB ooit terugkomt
    debugLog += msg + "<br>";

    // Beperk lengte
    if (debugLog.length() > 1000) {
        debugLog = debugLog.substring(debugLog.length() - 1000);
    }
}

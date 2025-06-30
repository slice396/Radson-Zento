#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
extern String rebootReason;
extern String debugLog;

void debugPrint(const String& msg);

#endif

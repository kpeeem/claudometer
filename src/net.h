#pragma once

#include <Arduino.h>

struct HttpResult {
    int status = 0;
    String body;
    String error;
    bool ok() const { return status >= 200 && status < 300; }
};

bool netConnectWifi(uint32_t timeoutMs = 25000);
bool netSyncClock(uint32_t timeoutMs = 15000);
void netProbeExit();
void netStartOta();
void netPollOta();
String netExitLabel();
HttpResult netRequest(const char *host, const char *method, const char *path, const String &headers,
                      const String &body, size_t bodyLimit = 24576);

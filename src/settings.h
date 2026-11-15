#pragma once

#include <Arduino.h>

struct Settings {
    String wifiSsid;
    String wifiPass;
    String proxyHost;
    uint16_t proxyPort;
    uint16_t refreshMinutes;
    String tz;
    uint8_t theme;

    String claudeAccess;
    String claudeRefresh;
    uint32_t claudeExpiry;

    String codexAccess;
    String codexRefresh;
    String codexAccount;
    uint32_t codexExpiry;

    bool cardOpus;
    bool cardFable;
    bool cardCodex;
    bool cardCodexWeek;
    bool cardClaudeDetail;
    bool cardIndoor;
    bool cardBattery;

    void load();
    void save();
    void saveClaudeTokens();
    void saveCodexTokens();
};

extern Settings settings;

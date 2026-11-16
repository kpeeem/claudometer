#pragma once

#include <Arduino.h>

enum : uint8_t { SOURCE_MAC = 0, SOURCE_PROXY = 1 };

struct Settings {
    String wifiSsid;
    String wifiPass;
    String proxyHost;
    uint16_t proxyPort;
    uint16_t refreshMinutes;
    String tz;
    uint8_t theme;
    uint8_t source;

    String claudeAccess;
    String claudeRefresh;
    uint32_t claudeExpiry;

    String codexAccess;
    String codexRefresh;
    String codexAccount;
    uint32_t codexExpiry;

    bool cardSession;
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

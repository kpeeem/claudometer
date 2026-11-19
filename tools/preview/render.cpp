#include <stdio.h>

#include <string>

#include <SPI.h>
#include <WiFi.h>

#include "link.h"
#include "net.h"
#include "quota.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

HardwareSerial Serial;
SPIClass SPI;
WiFiClass WiFi;

Provider claudeState{.name = "Claude"};
Provider codexState{.name = "Codex"};
Settings settings;
Agent agentState;

const char *healthText(Health health) {
    switch (health) {
        case Health::NoToken: return "no token";
        case Health::Ready: return "live";
        case Health::Stale: return "cached";
        case Health::Expired: return "sign in again";
        case Health::Throttled: return "rate limited";
        case Health::NetError: return "no data";
        case Health::BadData: return "bad reply";
        case Health::Waiting: return "waiting for Mac";
    }
    return "";
}

bool linkConnected() { return true; }
String netExitLabel() { return ""; }
void netPollOta() {}
bool sensorsIndoorValid() { return true; }
float sensorsTemperature() { return 22.4f; }
float sensorsHumidity() { return 41.0f; }
float sensorsBatteryVolts() { return 4.02f; }

static void window(Window &target, float used, long inSeconds) {
    target.valid = true;
    target.used = used;
    target.resetAt = time(nullptr) + inSeconds;
}

static void mock() {
    settings.refreshMinutes = 20;
    settings.tz = "UTC0";
    settings.source = SOURCE_MAC;
    settings.cardSession = true;
    settings.cardOpus = true;
    settings.cardFable = true;
    settings.cardCodex = true;
    settings.cardCodexWeek = true;
    settings.cardClaudeDetail = true;

    claudeState.health = Health::Ready;
    claudeState.plan = "Max 20x";
    window(claudeState.session, 42, 7850);
    window(claudeState.week, 61, 320000);
    window(claudeState.opus, 94, 320000);
    window(claudeState.sonnet, 37, 320000);
    window(claudeState.fable, 58, 320000);
    window(claudeState.opusSession, 42, 7850);
    claudeState.observedAt = time(nullptr);

    codexState.health = Health::Ready;
    codexState.plan = "Pro";
    window(codexState.session, 7, 12400);
    window(codexState.week, 9, 500000);
    codexState.observedAt = time(nullptr);

    agentState.state = "waiting";
    agentState.project = "weather-app";
    agentState.count = 1;
}

static void save(const std::string &folder, const char *name) {
    std::string path = folder + "/" + name + ".bin";
    FILE *out = fopen(path.c_str(), "wb");
    if (!out) {
        perror(path.c_str());
        exit(1);
    }
    fwrite(uiPlane(false), 1, uiPlaneBytes(), out);
    fwrite(uiPlane(true), 1, uiPlaneBytes(), out);
    fclose(out);
}

static void page(const std::string &folder, const char *name, uint8_t theme, Page which) {
    settings.theme = theme;
    uiApplyTheme();
    uiRenderPlanes(which);
    save(folder, name);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: render <folder>\n");
        return 2;
    }
    std::string folder = argv[1];
    mock();
    page(folder, "overview", 0, Page::Overview);
    page(folder, "theme-ink", 1, Page::Overview);
    page(folder, "theme-mono", 2, Page::Overview);
    page(folder, "detail", 0, Page::ClaudeDetail);

    settings.cardSession = false;
    agentState.state = "working";
    page(folder, "roomy", 0, Page::Overview);
    settings.cardSession = true;

    settings.theme = 0;
    uiApplyTheme();
    uiDumpHelp();
    save(folder, "help");
    uiDumpPortal();
    save(folder, "portal");
    return 0;
}

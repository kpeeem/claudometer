#include <Arduino.h>

#include "board.h"
#include "link.h"
#include "net.h"
#include "portal.h"
#include "quota.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

static constexpr uint32_t LONG_PRESS_MS = 800;
static constexpr uint32_t PORTAL_PRESS_MS = 2000;
static constexpr uint8_t PARTIALS_BEFORE_FULL = 20;

static Page pages[2];
static uint8_t pageCount = 1;
static uint8_t pageIndex = 0;
static uint32_t lastFetch = 0;
static uint32_t lastClock = 0;
static uint8_t partialsSinceFull = 0;
static bool helpShown = false;

struct Button {
    uint8_t pin;
    bool down = false;
    uint32_t since = 0;
    bool longFired = false;
};

static Button userBtn{PIN_USER_BTN};
static Button bootBtn{PIN_BOOT_BTN};

static void buildPages() {
    pageCount = 0;
    pages[pageCount++] = Page::Overview;
    if (settings.cardClaudeDetail) pages[pageCount++] = Page::ClaudeDetail;
    if (pageIndex >= pageCount) pageIndex = 0;
}

static void applySettings() {
    uiApplyTheme();
    setenv("TZ", settings.tz.c_str(), 1);
    tzset();
    buildPages();
}

static void renderFull() {
    if (helpShown) {
        uiHelpScreen();
        partialsSinceFull = 0;
        return;
    }
    uiRenderFull(pages[pageIndex]);
    partialsSinceFull = 0;
}

static void renderValues() {
    if (partialsSinceFull >= PARTIALS_BEFORE_FULL) {
        renderFull();
        return;
    }
    if (helpShown)
        uiHelpScreen();
    else
        uiRenderValues(pages[pageIndex]);
    partialsSinceFull++;
}

static void fetchAndRender() {
    if (settings.source == SOURCE_MAC) {
        netConnectWifi(8000);
        sensorsRead();
        lastFetch = millis();
        lastClock = millis();
        renderValues();
        return;
    }

    uiSetBusy(true);
    uiRenderValues(pages[pageIndex]);
    partialsSinceFull++;

    if (netConnectWifi()) {
        netSyncClock();
        netProbeExit();
        quotaRefreshAll();
    }
    sensorsRead();

    uiSetBusy(false);
    lastFetch = millis();
    lastClock = millis();
    renderValues();
}

static void enterPortal() {
    portalStart();
    while (portalRunning()) {
        portalLoop();
        if (digitalRead(PIN_BOOT_BTN) == LOW) {
            uint32_t start = millis();
            while (digitalRead(PIN_BOOT_BTN) == LOW && millis() - start < PORTAL_PRESS_MS) delay(20);
            if (millis() - start >= PORTAL_PRESS_MS) {
                portalStop();
                renderFull();
                return;
            }
        }
        delay(5);
    }
}

static void onShort(uint8_t pin) {
    if (pin == PIN_USER_BTN) {
        if (helpShown)
            helpShown = false;
        else
            pageIndex = (pageIndex + 1) % pageCount;
        renderValues();
        return;
    }
    helpShown = !helpShown;
    renderValues();
}

static void onLong(uint8_t pin) {
    if (pin == PIN_USER_BTN) {
        helpShown = false;
        fetchAndRender();
        return;
    }
    helpShown = false;
    enterPortal();
}

static void pollButton(Button &button) {
    bool pressed = digitalRead(button.pin) == LOW;
    uint32_t now = millis();

    if (pressed && !button.down) {
        button.down = true;
        button.since = now;
        button.longFired = false;
        return;
    }
    if (!pressed && button.down) {
        button.down = false;
        if (!button.longFired) onShort(button.pin);
        return;
    }
    if (pressed && button.down && !button.longFired) {
        uint32_t held = now - button.since;
        uint32_t threshold = button.pin == PIN_BOOT_BTN ? PORTAL_PRESS_MS : LONG_PRESS_MS;
        if (held >= threshold) {
            button.longFired = true;
            onLong(button.pin);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(150);
    log_i("%s quota board booting", BOARD_NAME);

    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    pinMode(PIN_LORA_EN, OUTPUT);
    digitalWrite(PIN_LORA_EN, LOW);
    pinMode(PIN_CODEC_EN, OUTPUT);
    digitalWrite(PIN_CODEC_EN, LOW);

    settings.load();
    applySettings();

    uiInit();
    sensorsInit();
    quotaInit();
    linkStart();

    bool wantPortal = digitalRead(PIN_BOOT_BTN) == LOW;
    if (!settings.wifiSsid.length()) wantPortal = true;
    if (wantPortal) {
        enterPortal();
        return;
    }

    String detail = settings.source == SOURCE_MAC
                        ? String("numbers from Agent Meter over Bluetooth")
                        : "proxy " + settings.proxyHost + ":" + String(settings.proxyPort);
    uiMessage("Connecting", ("Wi-Fi " + settings.wifiSsid).c_str(), detail.c_str());

    if (netConnectWifi()) {
        netSyncClock();
        if (settings.source == SOURCE_PROXY) netProbeExit();
    } else if (settings.source == SOURCE_PROXY) {
        uiMessage("No network", ("Cannot join " + settings.wifiSsid).c_str(),
                  "Hold BOOT for two seconds to open the setup portal.");
        return;
    } else {
        log_w("no wifi: Bluetooth only, no OTA until it joins");
    }

    if (settings.source == SOURCE_PROXY) quotaRefreshAll();
    sensorsRead();
    lastFetch = millis();
    lastClock = millis();
    renderFull();
}

#if UI_MOCK
static void mockWindow(Window &window, float used, long inSeconds) {
    window.valid = true;
    window.used = used;
    window.resetAt = time(nullptr) + inSeconds;
}

static void mockState() {
    claudeState.health = Health::Ready;
    claudeState.plan = "Max 20x";
    mockWindow(claudeState.session, 42, 7850);
    mockWindow(claudeState.week, 61, 320000);
    mockWindow(claudeState.opus, 94, 320000);
    mockWindow(claudeState.sonnet, 37, 320000);
    mockWindow(claudeState.fable, 58, 320000);
    mockWindow(claudeState.opusSession, 42, 7850);
    claudeState.observedAt = time(nullptr);

    codexState.health = Health::Ready;
    codexState.plan = "Pro";
    mockWindow(codexState.session, 7, 12400);
    mockWindow(codexState.week, 9, 500000);
    codexState.observedAt = time(nullptr);

    settings.cardClaudeDetail = true;
}
#endif

static void pollSerial() {
    while (Serial.available()) {
        int command = Serial.read();
#if UI_MOCK
        if (command == 'd') uiDumpPage(pages[pageIndex]);
        if (command == 's') uiDumpPortal();
        if (command == 'h') uiDumpHelp();
        if (command >= '0' && command <= '2') {
            settings.theme = command - '0';
            uiApplyTheme();
            renderFull();
        }
#endif
#if UI_MOCK
        if (command == 'm') {
            mockState();
            buildPages();
            renderFull();
        }
#endif
        if (command == 'p') {
            pageIndex = (pageIndex + 1) % pageCount;
            renderFull();
        }
        if (command == 'r') fetchAndRender();
    }
}

void loop() {
    netPollOta();

    switch (linkPoll()) {
        case LinkEvent::Settings:
            applySettings();
            renderFull();
            break;
        case LinkEvent::Data:
            lastFetch = millis();
            lastClock = millis();
            renderValues();
            break;
        case LinkEvent::None: break;
    }

    pollSerial();
    pollButton(userBtn);
    pollButton(bootBtn);

    uint32_t now = millis();
    if (now - lastFetch >= (uint32_t)settings.refreshMinutes * 60000UL) {
        fetchAndRender();
    } else if (uiFastRefreshReady() && now - lastClock >= 60000UL) {
        lastClock = now;
        renderValues();
    }
    delay(20);
}

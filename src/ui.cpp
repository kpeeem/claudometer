#include "ui.h"

#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <mbedtls/base64.h>

#include "board.h"

#if PANEL_3C
#include <GxEPD2_3C.h>
static GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(
    GxEPD2_420c_GDEY042Z98(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
#else
#include <GxEPD2_BW.h>
static GxEPD2_BW<GxEPD2_420_GYE042A87, GxEPD2_420_GYE042A87::HEIGHT> display(
    GxEPD2_420_GYE042A87(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
#endif

#include "link.h"
#include "net.h"
#include "quota.h"
#include "sensors.h"
#include "settings.h"

static U8G2_FOR_ADAFRUIT_GFX type;
static bool busyMark = false;

struct Theme {
    uint16_t bg;
    uint16_t ink;
    uint16_t accent;
};

static Theme theme = {GxEPD_WHITE, GxEPD_BLACK, GxEPD_RED};

void uiApplyTheme() {
    switch (settings.theme) {
        case 1: theme = {GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED}; break;
        case 2: theme = {GxEPD_WHITE, GxEPD_BLACK, GxEPD_BLACK}; break;
        default: theme = {GxEPD_WHITE, GxEPD_BLACK, GxEPD_RED}; break;
    }
}

const char *uiThemeName(uint8_t which) {
    switch (which) {
        case 1: return "Ink";
        case 2: return "Mono";
        default: return "Paper";
    }
}

namespace L {
constexpr int16_t W = EPD_WIDTH;
constexpr int16_t H = EPD_HEIGHT;
constexpr int16_t MARGIN = 14;
constexpr int16_t RIGHT = W - MARGIN;
constexpr int16_t CONTENT = RIGHT - MARGIN;

constexpr int16_t HEADER_BASE = 19;
constexpr int16_t HEADER_RULE = 27;

constexpr int16_t ROW_Y[3] = {40, 114, 188};
constexpr int16_t ROW_H = 62;

constexpr int16_t HERO_BASE = 26;
constexpr int16_t BAR_Y = 34;
constexpr int16_t BAR_H = 13;
constexpr int16_t META_BASE = 57;

constexpr int16_t FOOTER_RULE = 264;
constexpr int16_t FOOTER_BASE = 280;

constexpr float ALERT_AT = 90.0f;
}  // namespace L

#define F_HERO u8g2_font_logisoso30_tn
#define F_NAME u8g2_font_helvB14_tr
#define F_WORDMARK u8g2_font_10x20_t_cyrillic
#define F_STRONG u8g2_font_helvB10_tr
#define F_LABEL u8g2_font_helvB08_tr
#define F_META u8g2_font_helvR08_tr

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void dither(Adafruit_GFX &g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
    for (int16_t py = y; py < y + h; py++)
        for (int16_t px = x; px < x + w; px++)
            if (BAYER[py & 3][px & 3] < level) g.drawPixel(px, py, theme.ink);
}

static bool insideCapsule(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
    int16_t r = min(h, w) / 2;
    int16_t left = x + r, right = x + w - 1 - r;
    int16_t top = y + r, bottom = y + h - 1 - r;
    int16_t nx = px < left ? left : (px > right ? right : px);
    int16_t ny = py < top ? top : (py > bottom ? bottom : py);
    int32_t dx = px - nx, dy = py - ny;
    return dx * dx + dy * dy <= (int32_t)r * r;
}

static void capsule(Adafruit_GFX &g, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0) return;
    for (int16_t py = y; py < y + h; py++)
        for (int16_t px = x; px < x + w; px++)
            if (insideCapsule(px, py, x, y, w, h)) g.drawPixel(px, py, color);
}

static void capsuleDither(Adafruit_GFX &g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
    if (w <= 0) return;
    for (int16_t py = y; py < y + h; py++)
        for (int16_t px = x; px < x + w; px++)
            if (insideCapsule(px, py, x, y, w, h) && BAYER[py & 3][px & 3] < level)
                g.drawPixel(px, py, theme.ink);
}

static void bindType(Adafruit_GFX &g) {
    type.begin(g);
    type.setFontMode(1);
    type.setFontDirection(0);
    type.setForegroundColor(theme.ink);
    type.setBackgroundColor(theme.bg);
}

static void text(int16_t x, int16_t baseline, const uint8_t *font, const String &value) {
    type.setFont(font);
    type.setFontMode(1);
    type.setCursor(x, baseline);
    type.print(value);
}

static int16_t width(const uint8_t *font, const String &value) {
    type.setFont(font);
    type.setFontMode(1);
    return type.getUTF8Width(value.c_str());
}

static int16_t ascent(const uint8_t *font) {
    type.setFont(font);
    return type.getFontAscent();
}

static void textRight(int16_t right, int16_t baseline, const uint8_t *font, const String &value) {
    text(right - width(font, value), baseline, font, value);
}

static String percentText(float used) { return String((int)lroundf(constrain(used, 0.0f, 100.0f))); }

static void bar(Adafruit_GFX &g, int16_t y, float used, bool alert) {
    capsuleDither(g, L::MARGIN, y, L::CONTENT, L::BAR_H, 4);
    int16_t filled = (int16_t)lroundf(L::CONTENT * constrain(used, 0.0f, 100.0f) / 100.0f);
    if (filled < L::BAR_H && filled > 0) filled = L::BAR_H;
    capsule(g, L::MARGIN, y, filled, L::BAR_H, alert ? theme.accent : theme.ink);
}

static String relativeTime(time_t when) {
    time_t now = time(nullptr);
    if (!when || when <= now) return "";
    long left = when - now;
    long hours = left / 3600;
    long minutes = (left % 3600) / 60;
    if (hours >= 48) return "resets in " + String(hours / 24) + "d " + String(hours % 24) + "h";
    if (hours >= 1) return "resets in " + String(hours) + "h " + String(minutes) + "m";
    return "resets in " + String(max(1L, minutes)) + "m";
}

static String clockText() {
    time_t now = time(nullptr);
    if (now < 1760000000) return "--:--";
    tm local;
    localtime_r(&now, &local);
    char out[8];
    snprintf(out, sizeof(out), "%02d:%02d", local.tm_hour, local.tm_min);
    return out;
}

static void heroNumber(Adafruit_GFX &g, int16_t baseline, float used, bool alert) {
    String digits = percentText(used);
    int16_t digitsW = width(F_HERO, digits);
    int16_t signW = width(F_STRONG, "%");
    int16_t pad = alert ? 8 : 0;
    int16_t signX = L::RIGHT - pad - signW;
    int16_t digitsX = signX - 3 - digitsW;

    if (alert) {
        int16_t top = ascent(F_HERO) + 5;
        g.fillRoundRect(digitsX - pad, baseline - top, digitsW + 3 + signW + pad * 2, top + 7, 7, theme.accent);
        type.setForegroundColor(GxEPD_WHITE);
    }
    text(digitsX, baseline, F_HERO, digits);
    text(signX, baseline, F_STRONG, "%");
    if (alert) type.setForegroundColor(theme.ink);
}

struct Row {
    const char *title;
    const char *badge;
    const char *label;
    const Window *window;
    const Window *secondary;
    const char *secondaryLabel;
    Health health;
};

static void drawRow(Adafruit_GFX &g, int16_t y, const Row &row) {
    text(L::MARGIN, y + L::HERO_BASE, F_NAME, row.title);
    if (row.badge && *row.badge)
        text(L::MARGIN + width(F_NAME, row.title) + 9, y + L::HERO_BASE, F_META, row.badge);

    if (!row.window || !row.window->valid) {
        textRight(L::RIGHT, y + L::HERO_BASE, F_STRONG, healthText(row.health));
        capsuleDither(g, L::MARGIN, y + L::BAR_Y, L::CONTENT, L::BAR_H, 4);
        text(L::MARGIN, y + L::META_BASE, F_META, row.label);
        return;
    }

    bool alert = row.window->used >= L::ALERT_AT;
    heroNumber(g, y + L::HERO_BASE, row.window->used, alert);
    bar(g, y + L::BAR_Y, row.window->used, alert);

    text(L::MARGIN, y + L::META_BASE, F_META, row.label);
    String reset = relativeTime(row.window->resetAt);
    if (reset.length()) {
        int16_t dotX = L::MARGIN + width(F_META, row.label) + 6;
        g.fillRect(dotX, y + L::META_BASE - 3, 2, 2, theme.ink);
        text(dotX + 8, y + L::META_BASE, F_META, reset);
    }
    if (row.secondary && row.secondary->valid)
        textRight(L::RIGHT, y + L::META_BASE, F_LABEL,
                  String(row.secondaryLabel) + "  " + percentText(row.secondary->used) + "%");
}

static uint8_t pageRows(Page page, Row rows[3]) {
    uint8_t count = 0;
    if (page == Page::ClaudeDetail) {
        rows[count++] = {"Claude", "session", "5 hours", &claudeState.session, nullptr, "", claudeState.health};
        rows[count++] = {"Claude", "all models", "7 days", &claudeState.week, nullptr, "", claudeState.health};
        rows[count++] = {"Sonnet", "week", "7 days", &claudeState.sonnet, nullptr, "", claudeState.health};
        return count;
    }
    if (settings.cardOpus)
        rows[count++] = {"Opus",        "week",       "7 days", &claudeState.opus,
                         &claudeState.opusSession, "5h",     claudeState.health};
    if (settings.cardFable && count < 3)
        rows[count++] = {"Fable", "week", "7 days", &claudeState.fable, nullptr, "", claudeState.health};
    if (settings.cardCodex && count < 3)
        rows[count++] = {"Codex",
                         codexState.plan.c_str(),
                         "session",
                         &codexState.session,
                         settings.cardCodexWeek ? &codexState.week : nullptr,
                         "week",
                         codexState.health};
    return count;
}

static String footerText() {
    String extra;
    if (settings.cardIndoor && sensorsIndoorValid())
        extra = String(sensorsTemperature(), 1) + "C " + String((int)sensorsHumidity()) + "%rh";
    if (settings.cardBattery) {
        if (extra.length()) extra += "  ";
        extra += String(sensorsBatteryVolts(), 2) + "V";
    }

    time_t seen = max(claudeState.observedAt, codexState.observedAt);
    String stamp = "no reading yet";
    if (seen) {
        tm local;
        localtime_r(&seen, &local);
        char out[32];
        snprintf(out, sizeof(out), "updated %02d:%02d", local.tm_hour, local.tm_min);
        stamp = out;
    }
    String exit = netExitLabel();
    if (exit.length()) stamp += "   " + exit;
    if (extra.length()) stamp += "   " + extra;
    return stamp;
}

static const char *WORDMARK = "Клодометр";

static void headerChrome(Adafruit_GFX &g) {
    text(L::MARGIN, L::HEADER_BASE, F_WORDMARK, WORDMARK);
    g.drawFastHLine(L::MARGIN, L::HEADER_RULE, L::CONTENT, theme.accent);
}

static String agentText() {
    if (agentState.state == "waiting") return "needs you";
    if (agentState.state == "working")
        return agentState.count > 1 ? String(agentState.count) + " working" : "working";
    if (agentState.state == "done") return "done";
    if (agentState.state == "failed") return "failed";
    return "";
}

static void drawAgent(Adafruit_GFX &g, int16_t left, int16_t right) {
    String label = agentText();
    if (!label.length()) return;

    bool attention = agentState.state == "waiting";
    const uint8_t *font = attention ? F_STRONG : F_META;
    int16_t x = left;
    if (attention) {
        g.fillCircle(x + 3, L::HEADER_BASE - 4, 3, theme.ink);
        x += 11;
    }
    if (agentState.project.length()) {
        String full = label + "  " + agentState.project;
        if (x + width(font, full) <= right) label = full;
    }
    if (x + width(font, label) > right) return;
    text(x, L::HEADER_BASE, font, label);
}

static void headerValues(Adafruit_GFX &g) {
    String right = clockText();
    if (busyMark) right += " *";
    textRight(L::RIGHT, L::HEADER_BASE, F_STRONG, right);
    int16_t rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
    int16_t bars = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : rssi > -95 ? 1 : 0;
    int16_t x = L::RIGHT - width(F_STRONG, right) - 16;
    for (int16_t i = 0; i < 4; i++) {
        int16_t h = 3 + i * 3;
        if (i < bars)
            g.fillRect(x + i * 3, L::HEADER_BASE - h, 2, h, theme.ink);
        else
            dither(g, x + i * 3, L::HEADER_BASE - h, 2, h, 5);
    }
    drawAgent(g, L::MARGIN + width(F_WORDMARK, WORDMARK) + 14, x - 10);
}

static uint8_t visiblePages() { return settings.cardClaudeDetail ? 2 : 1; }

static void pageDots(Adafruit_GFX &g, Page page) {
    uint8_t total = visiblePages();
    if (total < 2) return;
    uint8_t active = page == Page::ClaudeDetail ? 1 : 0;
    int16_t y = L::FOOTER_BASE - 3;
    for (uint8_t i = 0; i < total; i++) {
        int16_t x = L::RIGHT - (total - i) * 9;
        if (i == active)
            g.fillCircle(x, y, 3, theme.ink);
        else
            g.drawCircle(x, y, 3, theme.ink);
    }
}

static void drawPage(Adafruit_GFX &g, Page page) {
    headerChrome(g);
    headerValues(g);
    Row rows[3];
    uint8_t count = pageRows(page, rows);
    for (uint8_t i = 0; i < count; i++) drawRow(g, L::ROW_Y[i], rows[i]);
    dither(g, L::MARGIN, L::FOOTER_RULE, L::CONTENT, 1, 6);
    text(L::MARGIN, L::FOOTER_BASE, F_META, footerText());
    pageDots(g, page);
}

static void helpLine(Adafruit_GFX &g, int16_t baseline, const char *key, const char *action, const char *what) {
    text(L::MARGIN, baseline, F_LABEL, key);
    text(L::MARGIN + 46, baseline, F_META, action);
    text(L::MARGIN + 116, baseline, F_META, what);
}

static void drawHelp(Adafruit_GFX &g) {
    headerChrome(g);
    headerValues(g);
    text(L::MARGIN, 52, F_NAME, "Buttons");

    helpLine(g, 76, "USER", "tap", "next page");
    helpLine(g, 94, "USER", "hold", "refresh now");
    helpLine(g, 112, "BOOT", "tap", "this screen");
    helpLine(g, 130, "BOOT", "hold 2s", "Wi-Fi, source, cards, theme");

    g.drawFastHLine(L::MARGIN, 150, L::CONTENT, theme.accent);

    String net = WiFi.status() == WL_CONNECTED
                     ? WiFi.SSID() + "   " + WiFi.localIP().toString() + "   " + String(WiFi.RSSI()) + " dBm"
                     : String("not connected");
    String numbers = settings.proxyHost + ":" + String(settings.proxyPort);
    if (netExitLabel().length()) numbers += "   " + netExitLabel();
    if (settings.source == SOURCE_MAC)
        numbers = linkConnected() ? "Agent Meter connected" : "waiting for Agent Meter";

    helpLine(g, 172, "WI-FI", "", net.c_str());
    helpLine(g, 190, settings.source == SOURCE_MAC ? "LINK" : "PROXY", "", numbers.c_str());
    helpLine(g, 208, "EVERY", "", (String(settings.refreshMinutes) + " min   theme " + uiThemeName(settings.theme)).c_str());
    helpLine(g, 226, "BOARD", "", (String(BOARD_NAME) + "   firmware " + FW_VERSION + "   ota quotaboard.local").c_str());

    dither(g, L::MARGIN, L::FOOTER_RULE, L::CONTENT, 1, 6);
    text(L::MARGIN, L::FOOTER_BASE, F_META, "tap BOOT to go back");
}

static void drawPortal(Adafruit_GFX &g, const char *ssid, const char *ip) {
    headerChrome(g);
    text(L::MARGIN, 72, F_NAME, "Setup portal");
    g.fillRect(L::MARGIN, 88, L::CONTENT, 1, theme.accent);
    g.fillRect(L::MARGIN, 145, L::CONTENT, 1, theme.ink);
    g.fillRect(L::MARGIN, 96, 3, 42, theme.accent);
    text(L::MARGIN + 16, 114, F_STRONG, String("Wi-Fi   ") + ssid);
    text(L::MARGIN + 16, 136, F_STRONG, String("Open    http://") + ip);
    text(L::MARGIN, 176, F_META, "Choose where the numbers come from, the cards and");
    text(L::MARGIN, 192, F_META, "the theme, then press Save. The board restarts.");
    text(L::MARGIN, 216, F_META, "Hold BOOT for two seconds to leave without saving.");
    dither(g, L::MARGIN, L::FOOTER_RULE, L::CONTENT, 1, 6);
    text(L::MARGIN, L::FOOTER_BASE, F_META, BOARD_NAME);
    textRight(L::RIGHT, L::FOOTER_BASE, F_META, "setup mode");
}

static void serviceWhileBusy(const void *) {
    netPollOta();
    delay(1);
}

void uiInit() {
    SPI.end();
    SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.epd2.setBusyCallback(serviceWhileBusy);
    display.setFullWindow();
    bindType(display);
}

void uiSetBusy(bool busy) { busyMark = busy; }

bool uiSupportsPartial() { return PANEL_3C == 0; }

void uiRenderFull(Page page) {
    bindType(display);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(theme.bg);
        drawPage(display, page);
    } while (display.nextPage());
}

void uiRenderValues(Page page) {
#if PANEL_3C
    uiRenderFull(page);
#else
    bindType(display);
    const int16_t bands[4][2] = {{0, L::HEADER_RULE + 1},
                                 {L::ROW_Y[0], L::ROW_H},
                                 {L::ROW_Y[1], L::ROW_H},
                                 {L::ROW_Y[2], (int16_t)(L::H - L::ROW_Y[2])}};
    for (auto &band : bands) {
        display.setPartialWindow(0, band[0], L::W, band[1]);
        display.firstPage();
        do {
            display.fillScreen(theme.bg);
            drawPage(display, page);
        } while (display.nextPage());
    }
#endif
}

void uiMessage(const char *title, const char *line1, const char *line2) {
    bindType(display);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(theme.bg);
        headerChrome(display);
        headerValues(display);
        text(L::MARGIN, 120, F_NAME, title);
        text(L::MARGIN, 148, F_META, line1 ? line1 : "");
        text(L::MARGIN, 165, F_META, line2 ? line2 : "");
        dither(display, L::MARGIN, L::FOOTER_RULE, L::CONTENT, 1, 6);
        text(L::MARGIN, L::FOOTER_BASE, F_META, BOARD_NAME);
    } while (display.nextPage());
}

void uiHelpScreen() {
    bindType(display);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(theme.bg);
        drawHelp(display);
    } while (display.nextPage());
}

void uiPortalScreen(const char *ssid, const char *ip) {
    bindType(display);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(theme.bg);
        drawPortal(display, ssid, ip);
    } while (display.nextPage());
}

void uiHibernate() { display.hibernate(); }

#if UI_MOCK
class PlaneCanvas : public Adafruit_GFX {
  public:
    PlaneCanvas() : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT) {}
    uint8_t black[EPD_WIDTH / 8 * EPD_HEIGHT];
    uint8_t red[EPD_WIDTH / 8 * EPD_HEIGHT];

    void clear() {
        memset(black, 0, sizeof(black));
        memset(red, 0, sizeof(red));
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) return;
        size_t at = y * (EPD_WIDTH / 8) + (x >> 3);
        uint8_t bit = 0x80 >> (x & 7);
        black[at] &= ~bit;
        red[at] &= ~bit;
        if (color == GxEPD_RED)
            red[at] |= bit;
        else if (color != GxEPD_WHITE)
            black[at] |= bit;
    }
};

static PlaneCanvas canvas;

static void emitPlane(const uint8_t *raw, size_t len) {
    for (size_t at = 0; at < len; at += 48) {
        size_t chunk = min((size_t)48, len - at);
        unsigned char line[72];
        size_t written = 0;
        if (mbedtls_base64_encode(line, sizeof(line), &written, raw + at, chunk) == 0) {
            line[written] = 0;
            Serial.println((const char *)line);
        }
    }
}

static void dumpCanvas() {
    Serial.println("--DUMP-BEGIN--");
    Serial.println("--PLANE-BLACK--");
    emitPlane(canvas.black, sizeof(canvas.black));
    Serial.println("--PLANE-RED--");
    emitPlane(canvas.red, sizeof(canvas.red));
    Serial.println("--DUMP-END--");
}

void uiDumpPage(Page page) {
    canvas.clear();
    canvas.fillScreen(theme.bg);
    bindType(canvas);
    drawPage(canvas, page);
    bindType(display);
    dumpCanvas();
}

void uiDumpPortal() {
    canvas.clear();
    canvas.fillScreen(theme.bg);
    bindType(canvas);
    drawPortal(canvas, "Quota-Setup / quotaboard", "192.168.4.1");
    bindType(display);
    dumpCanvas();
}

void uiDumpHelp() {
    canvas.clear();
    canvas.fillScreen(theme.bg);
    bindType(canvas);
    drawHelp(canvas);
    bindType(display);
    dumpCanvas();
}
#endif

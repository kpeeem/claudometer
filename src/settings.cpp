#include "settings.h"

#include <Preferences.h>

#include "secrets.h"

Settings settings;

static Preferences store;

void Settings::load() {
    store.begin("quotaboard", true);
    wifiSsid = store.getString("ssid", DEFAULT_WIFI_SSID);
    wifiPass = store.getString("pass", DEFAULT_WIFI_PASS);
    proxyHost = store.getString("phost", DEFAULT_PROXY_HOST);
    proxyPort = store.getUShort("pport", DEFAULT_PROXY_PORT);
    refreshMinutes = store.getUShort("every", 20);
    tz = store.getString("tz", "MSK-3");
    theme = store.getUChar("theme", 0);

    claudeAccess = store.getString("c_at", "");
    claudeRefresh = store.getString("c_rt", "");
    claudeExpiry = store.getUInt("c_exp", 0);

    codexAccess = store.getString("x_at", "");
    codexRefresh = store.getString("x_rt", "");
    codexAccount = store.getString("x_acc", "");
    codexExpiry = store.getUInt("x_exp", 0);

    cardOpus = store.getBool("k_op", true);
    cardFable = store.getBool("k_fa", true);
    cardCodex = store.getBool("k_cx", true);
    cardCodexWeek = store.getBool("k_xw", true);
    cardClaudeDetail = store.getBool("k_cd", false);
    cardIndoor = store.getBool("k_in", false);
    cardBattery = store.getBool("k_bt", false);
    store.end();

    if (refreshMinutes < 5) refreshMinutes = 5;
    if (refreshMinutes > 240) refreshMinutes = 240;
    if (theme > 2) theme = 0;
}

void Settings::save() {
    store.begin("quotaboard", false);
    store.putString("ssid", wifiSsid);
    store.putString("pass", wifiPass);
    store.putString("phost", proxyHost);
    store.putUShort("pport", proxyPort);
    store.putUShort("every", refreshMinutes);
    store.putString("tz", tz);
    store.putUChar("theme", theme);
    store.putBool("k_op", cardOpus);
    store.putBool("k_fa", cardFable);
    store.putBool("k_cx", cardCodex);
    store.putBool("k_xw", cardCodexWeek);
    store.putBool("k_cd", cardClaudeDetail);
    store.putBool("k_in", cardIndoor);
    store.putBool("k_bt", cardBattery);
    store.end();
}

void Settings::saveClaudeTokens() {
    store.begin("quotaboard", false);
    store.putString("c_at", claudeAccess);
    store.putString("c_rt", claudeRefresh);
    store.putUInt("c_exp", claudeExpiry);
    store.end();
}

void Settings::saveCodexTokens() {
    store.begin("quotaboard", false);
    store.putString("x_at", codexAccess);
    store.putString("x_rt", codexRefresh);
    store.putString("x_acc", codexAccount);
    store.putUInt("x_exp", codexExpiry);
    store.end();
}

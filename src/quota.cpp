#include "quota.h"

#include <ArduinoJson.h>

#include "net.h"
#include "settings.h"

Provider claudeState{.name = "Claude"};
Provider codexState{.name = "Codex"};

static const char *CLAUDE_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
static const char *CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";
static const char *CLAUDE_UA = "User-Agent: claude-code/2.1.0\r\n";

const char *healthText(Health health) {
    switch (health) {
        case Health::NoToken: return "no token";
        case Health::Ready: return "live";
        case Health::Stale: return "cached";
        case Health::Expired: return "sign in again";
        case Health::Throttled: return "rate limited";
        case Health::NetError: return "no data";
        case Health::BadData: return "bad reply";
    }
    return "";
}

static time_t epochFromUtc(const tm &parts) {
    static const int cumulative[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long year = parts.tm_year + 1900;
    long days = (year - 1970) * 365 + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400 - 477;
    days += cumulative[parts.tm_mon];
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (leap && parts.tm_mon > 1) days += 1;
    days += parts.tm_mday - 1;
    return (time_t)days * 86400 + parts.tm_hour * 3600 + parts.tm_min * 60 + parts.tm_sec;
}

static time_t parseInstant(JsonVariantConst value) {
    if (value.is<long long>()) {
        long long raw = value.as<long long>();
        return raw > 100000000000LL ? (time_t)(raw / 1000) : (time_t)raw;
    }
    if (value.is<double>()) return (time_t)value.as<double>();
    const char *text = value.as<const char *>();
    if (!text) return 0;
    tm parts{};
    int year, mon, day, hour, min;
    double sec = 0;
    if (sscanf(text, "%d-%d-%dT%d:%d:%lf", &year, &mon, &day, &hour, &min, &sec) < 5) return 0;
    parts.tm_year = year - 1900;
    parts.tm_mon = mon - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = min;
    parts.tm_sec = (int)sec;
    return epochFromUtc(parts);
}

static Window readWindow(JsonObjectConst node) {
    Window out;
    if (node.isNull()) return out;

    JsonVariantConst used = node["utilization"];
    if (used.isNull()) used = node["used_percent"];
    if (used.isNull()) used = node["percent"];
    if (used.isNull()) used = node["usage_percent"];
    if (used.isNull()) return out;

    float value = used.as<float>();
    if (!isfinite(value)) return out;
    if (value <= 1.0f && node["utilization"].is<double>()) value *= 100.0f;
    out.used = constrain(value, 0.0f, 100.0f);

    JsonVariantConst reset = node["resets_at"];
    if (reset.isNull()) reset = node["reset_at"];
    if (reset.isNull()) reset = node["resets_at_utc"];
    out.resetAt = parseInstant(reset);
    out.valid = true;
    return out;
}

static bool refreshOAuth(const char *host, const char *path, const char *clientId, String &access,
                         String &refresh, uint32_t &expiry) {
    if (!refresh.length()) return false;

    JsonDocument request;
    request["grant_type"] = "refresh_token";
    request["refresh_token"] = refresh;
    request["client_id"] = clientId;
    String payload;
    serializeJson(request, payload);

    HttpResult response = netRequest(host, "POST", path,
                                     "Content-Type: application/json\r\nAccept: application/json\r\n", payload);
    if (!response.ok()) {
        log_w("%s refresh failed: status=%d err=%s", host, response.status, response.error.c_str());
        return false;
    }

    JsonDocument body;
    if (deserializeJson(body, response.body)) return false;
    const char *token = body["access_token"];
    if (!token) return false;
    access = token;
    if (body["refresh_token"].is<const char *>()) refresh = body["refresh_token"].as<const char *>();
    uint32_t lifetime = body["expires_in"] | 28800;
    expiry = (uint32_t)time(nullptr) + lifetime;
    log_i("%s refreshed, valid for %us", host, lifetime);
    return true;
}

static void finish(Provider &state, Health health) {
    if (health == Health::Ready) {
        state.observedAt = time(nullptr);
        state.health = Health::Ready;
        return;
    }
    state.health = state.hasData() && health == Health::NetError ? Health::Stale : health;
}

static void refreshClaude() {
    Provider &state = claudeState;
    if (!settings.claudeAccess.length()) {
        state.health = Health::NoToken;
        return;
    }

    time_t now = time(nullptr);
    if (settings.claudeExpiry && now > (time_t)settings.claudeExpiry - 300) {
        if (refreshOAuth("console.anthropic.com", "/v1/oauth/token", CLAUDE_CLIENT_ID, settings.claudeAccess,
                         settings.claudeRefresh, settings.claudeExpiry))
            settings.saveClaudeTokens();
    }

    String headers = "Authorization: Bearer " + settings.claudeAccess + "\r\n";
    headers += "anthropic-beta: oauth-2025-04-20\r\n";
    headers += "Accept: application/json\r\n";
    headers += CLAUDE_UA;

    HttpResult response = netRequest("api.anthropic.com", "GET", "/api/oauth/usage", headers, "");
    if (response.status == 401 || response.status == 403) {
        if (refreshOAuth("console.anthropic.com", "/v1/oauth/token", CLAUDE_CLIENT_ID, settings.claudeAccess,
                         settings.claudeRefresh, settings.claudeExpiry)) {
            settings.saveClaudeTokens();
            headers = "Authorization: Bearer " + settings.claudeAccess + "\r\n";
            headers += "anthropic-beta: oauth-2025-04-20\r\nAccept: application/json\r\n";
            headers += CLAUDE_UA;
            response = netRequest("api.anthropic.com", "GET", "/api/oauth/usage", headers, "");
        } else {
            finish(state, Health::Expired);
            return;
        }
    }
    if (response.status == 429) {
        finish(state, Health::Throttled);
        return;
    }
    if (!response.ok()) {
        finish(state, Health::NetError);
        return;
    }

    JsonDocument body;
    if (deserializeJson(body, response.body)) {
        log_w("claude usage not json: %.120s", response.body.c_str());
        finish(state, Health::BadData);
        return;
    }

    JsonObjectConst root = body.as<JsonObjectConst>();
    JsonObjectConst limits = root["rate_limits"].is<JsonObjectConst>() ? root["rate_limits"].as<JsonObjectConst>() : root;

    Window session = readWindow(limits["five_hour"]);
    if (!session.valid) session = readWindow(limits["session"]);
    Window week = readWindow(limits["seven_day"]);
    if (!week.valid) week = readWindow(limits["weekly"]);
    Window opus = readWindow(limits["seven_day_opus"]);
    Window sonnet = readWindow(limits["seven_day_sonnet"]);
    Window fable = readWindow(limits["seven_day_fable"]);
    Window opusSession = readWindow(limits["five_hour_opus"]);

    for (JsonObjectConst entry : limits["model_scoped"].as<JsonArrayConst>()) {
        const char *label = entry["name"] | entry["model"] | "";
        Window parsed = readWindow(entry);
        if (!parsed.valid) continue;
        String name(label);
        name.toLowerCase();
        if (name.indexOf("opus") >= 0 && !opus.valid) opus = parsed;
        if (name.indexOf("sonnet") >= 0 && !sonnet.valid) sonnet = parsed;
        if (name.indexOf("fable") >= 0 && !fable.valid) fable = parsed;
    }

    if (!session.valid && !week.valid && !opus.valid && !sonnet.valid && !fable.valid) {
        log_w("claude usage unparsed: %.200s", response.body.c_str());
        finish(state, Health::BadData);
        return;
    }

    state.session = session;
    state.week = week;
    state.opus = opus;
    state.sonnet = sonnet;
    state.fable = fable;
    state.opusSession = opusSession.valid ? opusSession : session;
    if (root["account"]["plan"].is<const char *>()) state.plan = root["account"]["plan"].as<const char *>();
    finish(state, Health::Ready);
}

static void refreshCodex() {
    Provider &state = codexState;
    if (!settings.codexAccess.length()) {
        state.health = Health::NoToken;
        return;
    }

    time_t now = time(nullptr);
    if (settings.codexExpiry && now > (time_t)settings.codexExpiry - 300) {
        if (refreshOAuth("auth.openai.com", "/oauth/token", CODEX_CLIENT_ID, settings.codexAccess,
                         settings.codexRefresh, settings.codexExpiry))
            settings.saveCodexTokens();
    }

    String headers = "Authorization: Bearer " + settings.codexAccess + "\r\n";
    headers += "Accept: application/json\r\n";
    if (settings.codexAccount.length()) headers += "ChatGPT-Account-Id: " + settings.codexAccount + "\r\n";

    HttpResult response = netRequest("chatgpt.com", "GET", "/backend-api/wham/usage", headers, "");
    if (response.status == 401 || response.status == 403) {
        if (refreshOAuth("auth.openai.com", "/oauth/token", CODEX_CLIENT_ID, settings.codexAccess,
                         settings.codexRefresh, settings.codexExpiry)) {
            settings.saveCodexTokens();
            headers = "Authorization: Bearer " + settings.codexAccess + "\r\nAccept: application/json\r\n";
            if (settings.codexAccount.length()) headers += "ChatGPT-Account-Id: " + settings.codexAccount + "\r\n";
            response = netRequest("chatgpt.com", "GET", "/backend-api/wham/usage", headers, "");
        } else {
            finish(state, Health::Expired);
            return;
        }
    }
    if (response.status == 429) {
        finish(state, Health::Throttled);
        return;
    }
    if (!response.ok()) {
        finish(state, Health::NetError);
        return;
    }

    JsonDocument body;
    if (deserializeJson(body, response.body)) {
        finish(state, Health::BadData);
        return;
    }

    JsonObjectConst limits = body["rate_limit"];
    Window primary = readWindow(limits["primary_window"]);
    Window secondary = readWindow(limits["secondary_window"]);
    if (!primary.valid && !secondary.valid) {
        log_w("codex usage unparsed: %.200s", response.body.c_str());
        finish(state, Health::BadData);
        return;
    }

    state.session = primary;
    state.week = secondary;
    if (body["plan_type"].is<const char *>()) {
        state.plan = body["plan_type"].as<const char *>();
        if (state.plan.length()) state.plan.setCharAt(0, toupper(state.plan[0]));
    }
    finish(state, Health::Ready);
}

void quotaInit() {
    claudeState.health = settings.claudeAccess.length() ? Health::NetError : Health::NoToken;
    codexState.health = settings.codexAccess.length() ? Health::NetError : Health::NoToken;
}

void quotaRefreshAll() {
    refreshClaude();
    refreshCodex();
}

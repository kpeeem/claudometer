#include "link.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <sys/time.h>

#include "board.h"
#include "quota.h"
#include "settings.h"

static const char *SERVICE_UUID = "ae4c9350-5442-48a2-8f62-288e8b34fc7c";
static const char *QUOTA_UUID = "ae4c9351-5442-48a2-8f62-288e8b34fc7c";
static const char *CONFIG_UUID = "ae4c9352-5442-48a2-8f62-288e8b34fc7c";
static const uint8_t PROTOCOL = 1;

Agent agentState;

static NimBLECharacteristic *configChar = nullptr;
static SemaphoreHandle_t lock = nullptr;
static String pendingQuota;
static String pendingConfig;
static bool pendingDescribe = false;
static bool started = false;
static bool staleShown = false;

class WriteSink : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
        String *target = characteristic == configChar ? &pendingConfig : &pendingQuota;
        xSemaphoreTake(lock, portMAX_DELAY);
        *target = characteristic->getValue().c_str();
        xSemaphoreGive(lock);
    }
};

class ServerSink : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
        log_i("link: central connected, mtu=%u", info.getMTU());
        xSemaphoreTake(lock, portMAX_DELAY);
        pendingDescribe = true;
        xSemaphoreGive(lock);
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
        log_i("link: central gone, reason=%d", reason);
    }
};

static WriteSink writeSink;
static ServerSink serverSink;

static Window readWindow(JsonArrayConst node) {
    Window out;
    if (node.isNull() || node.size() < 1) return out;
    float used = node[0].as<float>();
    if (!isfinite(used)) return out;
    out.used = constrain(used, 0.0f, 100.0f);
    if (node.size() > 1) out.resetAt = (time_t)node[1].as<long long>();
    if (node.size() > 2) out.age = (uint32_t)constrain(node[2].as<long long>(), 0LL, 2592000LL);
    out.valid = true;
    return out;
}

static Health readHealth(const char *code) {
    if (!code) return Health::Waiting;
    if (!strcmp(code, "ok")) return Health::Ready;
    if (!strcmp(code, "stale")) return Health::Stale;
    if (!strcmp(code, "expired")) return Health::Expired;
    if (!strcmp(code, "limit")) return Health::Throttled;
    if (!strcmp(code, "err")) return Health::NetError;
    return Health::Waiting;
}

static void applySide(Provider &state, JsonObjectConst node, time_t at, bool claude) {
    if (node.isNull()) return;
    state.health = readHealth(node["state"]);
    if (node["plan"].is<const char *>()) state.plan = node["plan"].as<const char *>();
    state.session = readWindow(node["session"]);
    state.week = readWindow(node["week"]);
    if (claude) {
        state.opus = readWindow(node["opus"]);
        state.sonnet = readWindow(node["sonnet"]);
        state.fable = readWindow(node["fable"]);
        Window opusSession = readWindow(node["opusSession"]);
        state.opusSession = opusSession.valid ? opusSession : state.session;
    }
    if (state.hasData()) state.observedAt = at;
}

static bool applyAgent(JsonObjectConst node) {
    String was = agentState.state;
    if (node.isNull()) {
        agentState = Agent{};
    } else {
        agentState.state = node["state"].is<const char *>() ? node["state"].as<const char *>() : "";
        agentState.project = node["project"].is<const char *>() ? node["project"].as<const char *>() : "";
        agentState.count = node["count"] | 0;
    }
    if (agentState.state == was) return false;
    return agentState.state == "waiting" || agentState.state == "done" || agentState.state == "failed";
}

static bool applyQuota(const String &payload) {
    if (settings.source != SOURCE_MAC) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        log_w("link: quota payload is not json (%u bytes)", (unsigned)payload.length());
        return false;
    }
    if (doc["v"].as<uint8_t>() != PROTOCOL) {
        log_w("link: quota payload version %d, expected %u", doc["v"].as<int>(), PROTOCOL);
        return false;
    }

    time_t at = doc["at"].as<long long>();
    if (at < 1760000000) at = 0;
    if (at && time(nullptr) < 1760000000) {
        timeval wall{.tv_sec = at, .tv_usec = 0};
        settimeofday(&wall, nullptr);
        log_i("link: clock set from the Mac");
    }
    time_t now = time(nullptr);
    if (!at || at > now + 300) at = now;

    bool wake = staleShown || (!claudeState.observedAt && !codexState.observedAt);
    applySide(claudeState, doc["claude"], at, true);
    applySide(codexState, doc["codex"], at, false);
    staleShown = false;

    static uint32_t lastAgentWake = 0;
    if (applyAgent(doc["agent"]) && (!lastAgentWake || millis() - lastAgentWake > 120000UL)) {
        lastAgentWake = millis();
        wake = true;
    }
    log_i("link: quota in, claude=%s codex=%s", healthText(claudeState.health), healthText(codexState.health));
    return wake;
}

static bool applyConfig(const String &payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        log_w("link: config payload is not json");
        return false;
    }

    if (doc["theme"].is<int>()) settings.theme = constrain(doc["theme"].as<int>(), 0, 2);
    if (doc["every"].is<int>()) settings.refreshMinutes = constrain(doc["every"].as<int>(), 5, 240);
    if (doc["source"].is<int>()) settings.source = doc["source"].as<int>() ? SOURCE_PROXY : SOURCE_MAC;
    if (doc["tz"].is<const char *>()) {
        String zone = doc["tz"].as<const char *>();
        if (zone.length() && zone.length() <= 48) settings.tz = zone;
    }

    JsonObjectConst cards = doc["cards"];
    if (!cards.isNull()) {
        if (cards["opus"].is<bool>()) settings.cardOpus = cards["opus"];
        if (cards["fable"].is<bool>()) settings.cardFable = cards["fable"];
        if (cards["codex"].is<bool>()) settings.cardCodex = cards["codex"];
        if (cards["codexWeek"].is<bool>()) settings.cardCodexWeek = cards["codexWeek"];
        if (cards["detail"].is<bool>()) settings.cardClaudeDetail = cards["detail"];
        if (cards["indoor"].is<bool>()) settings.cardIndoor = cards["indoor"];
        if (cards["battery"].is<bool>()) settings.cardBattery = cards["battery"];
    }

    settings.save();
    log_i("link: config saved, source=%u theme=%u every=%u", settings.source, settings.theme,
          settings.refreshMinutes);
    return true;
}

static void publish() {
    if (!configChar) return;

    JsonDocument doc;
    doc["v"] = PROTOCOL;
    doc["board"] = BOARD_NAME;
    doc["fw"] = FW_VERSION;
    doc["source"] = settings.source;
    doc["theme"] = settings.theme;
    doc["every"] = settings.refreshMinutes;
    doc["tz"] = settings.tz;
    JsonObject cards = doc["cards"].to<JsonObject>();
    cards["opus"] = settings.cardOpus;
    cards["fable"] = settings.cardFable;
    cards["codex"] = settings.cardCodex;
    cards["codexWeek"] = settings.cardCodexWeek;
    cards["detail"] = settings.cardClaudeDetail;
    cards["indoor"] = settings.cardIndoor;
    cards["battery"] = settings.cardBattery;
    doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();

    String out;
    serializeJson(doc, out);
    configChar->setValue(out);
    configChar->notify();
}

static bool markStale() {
    if (settings.source != SOURCE_MAC || staleShown) return false;
    time_t seen = max(claudeState.observedAt, codexState.observedAt);
    if (!seen) return false;
    if (time(nullptr) - seen <= (time_t)settings.refreshMinutes * 180) return false;

    staleShown = true;
    if (claudeState.hasData()) claudeState.health = Health::Stale;
    if (codexState.hasData()) codexState.health = Health::Stale;
    log_i("link: no push since %ld, numbers marked cached", (long)seen);
    return true;
}

void linkStart() {
    if (started) return;
    lock = xSemaphoreCreateMutex();

    NimBLEDevice::init("quotaboard");
    NimBLEDevice::setMTU(517);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&serverSink, false);

    NimBLEService *service = server->createService(SERVICE_UUID);
    service->createCharacteristic(QUOTA_UUID, NIMBLE_PROPERTY::WRITE)->setCallbacks(&writeSink);
    configChar = service->createCharacteristic(
        CONFIG_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    configChar->setCallbacks(&writeSink);
    service->start();

    NimBLEAdvertisementData scanResponse;
    scanResponse.setName("quotaboard");

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponseData(scanResponse);
    advertising->enableScanResponse(true);

    started = true;
    publish();
    log_i("link: advertising as quotaboard: %s", NimBLEDevice::startAdvertising() ? "up" : "failed");
}

LinkEvent linkPoll() {
    if (!started) return LinkEvent::None;

    String quota;
    String config;
    bool describe;
    xSemaphoreTake(lock, portMAX_DELAY);
    quota = pendingQuota;
    config = pendingConfig;
    describe = pendingDescribe;
    pendingQuota = "";
    pendingConfig = "";
    pendingDescribe = false;
    xSemaphoreGive(lock);

    bool data = quota.length() && applyQuota(quota);
    bool changed = config.length() && applyConfig(config);
    if (describe || changed) publish();

    if (changed) return LinkEvent::Settings;
    return data || markStale() ? LinkEvent::Data : LinkEvent::None;
}

bool linkConnected() {
    NimBLEServer *server = started ? NimBLEDevice::getServer() : nullptr;
    return server && server->getConnectedCount() > 0;
}

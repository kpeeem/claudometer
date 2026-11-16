#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "settings.h"
#include "ui.h"

static const char *AP_SSID = "Quota-Setup";
static const char *AP_PASS = "quotaboard";

static WebServer server(80);
static DNSServer dns;
static bool running = false;

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Quota board setup</title><style>
:root{--ink:#1a1a1a;--muted:#6b6b6b;--line:#e2e2e2;--bg:#fbfbfa}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:33rem;margin:0 auto;padding:2.5rem 1.25rem 4rem}
h1{font-size:1.35rem;font-weight:600;letter-spacing:-.01em;margin:0 0 .25rem}
p.sub{color:var(--muted);margin:0 0 2rem;font-size:.9rem}
fieldset{border:0;border-top:1px solid var(--line);margin:0 0 1.75rem;padding:1.5rem 0 0}
legend{padding:0 .6rem 0 0;font-size:.75rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
label{display:block;margin:0 0 1rem}
label span{display:block;font-size:.8rem;color:var(--muted);margin-bottom:.3rem}
select{width:100%;padding:.55rem .7rem;border:1px solid var(--line);border-radius:6px;background:#fff;font:inherit;font-size:.9rem}
input[type=text],input[type=password],input[type=number]{width:100%;padding:.55rem .7rem;border:1px solid var(--line);border-radius:6px;background:#fff;font:inherit;font-size:.9rem}
input:focus{outline:0;border-color:#9a9a9a}
.row{display:flex;gap:.75rem}.row label{flex:1}
.check{display:flex;align-items:center;gap:.55rem;margin:0 0 .6rem;font-size:.9rem}
.check input{margin:0}
button{width:100%;padding:.7rem;border:0;border-radius:6px;background:var(--ink);color:#fff;font:inherit;font-weight:500;cursor:pointer}
code{background:#f0f0ef;padding:.1rem .3rem;border-radius:3px;font-size:.85em}
.note{font-size:.8rem;color:var(--muted);margin:.4rem 0 0}
</style><main><h1>Quota board</h1><p class="sub">NM-EPD-420 &middot; tokens stay in this board's NVS</p><form method="POST" action="/save">)HTML";

static const char PAGE_TAIL[] PROGMEM = R"HTML(<button type="submit">Save and restart</button></form></main></html>)HTML";

static String field(const char *name, const char *label, const String &value, const char *type = "text",
                    const char *hint = nullptr) {
    String out = "<label><span>" + String(label) + "</span><input type=\"" + type + "\" name=\"" + name +
                 "\" value=\"" + value + "\"></label>";
    if (hint) out += "<p class=\"note\">" + String(hint) + "</p>";
    return out;
}

static String check(const char *name, const char *label, bool on) {
    return "<div class=\"check\"><input type=\"checkbox\" id=\"" + String(name) + "\" name=\"" + name + "\" value=\"1\"" +
           (on ? " checked" : "") + "><label for=\"" + name + "\" style=\"margin:0\">" + label + "</label></div>";
}

static void handleRoot() {
    String body = FPSTR(PAGE_HEAD);

    body += "<fieldset><legend>Numbers</legend><label><span>Where the board gets them</span>"
            "<select name=\"src\">";
    body += String("<option value=\"0\"") + (settings.source == SOURCE_MAC ? " selected" : "") +
            ">Agent Meter on the Mac, over Bluetooth</option>";
    body += String("<option value=\"1\"") + (settings.source == SOURCE_PROXY ? " selected" : "") +
            ">This board, through the proxy</option>";
    body += "</select></label><p class=\"note\">Over Bluetooth the board holds no tokens and the Mac pushes "
            "the numbers it already has. The proxy path is the fallback and needs the tokens below.</p>"
            "</fieldset>";

    body += "<fieldset><legend>Network</legend>";
    body += field("ssid", "Wi-Fi network", settings.wifiSsid);
    body += field("pass", "Wi-Fi password", "", "password",
                  settings.wifiPass.length() ? "Stored. Leave blank to keep it." : nullptr);
    body += "<div class=\"row\">";
    body += field("phost", "Proxy host", settings.proxyHost);
    body += field("pport", "Port", String(settings.proxyPort), "number");
    body += "</div>";
    body += "<p class=\"note\">Wi-Fi is always joined, so over-the-air updates keep working. All provider "
            "traffic on the proxy path goes through this HTTP CONNECT proxy.</p>";
    body += "<div class=\"row\">";
    body += field("tz", "Timezone", settings.tz);
    body += field("every", "Refresh, minutes", String(settings.refreshMinutes), "number");
    body += "</div></fieldset>";

    body += "<fieldset><legend>Claude</legend>";
    body += field("c_at", "Access token", "", "password",
                  settings.claudeAccess.length() ? "Stored. Leave blank to keep it." : nullptr);
    body += field("c_rt", "Refresh token", "", "password",
                  "From <code>~/.claude/.credentials.json</code> or the login keychain item "
                  "<code>Claude Code-credentials</code>.");
    body += "</fieldset>";

    body += "<fieldset><legend>Codex</legend>";
    body += field("x_at", "Access token", "", "password",
                  settings.codexAccess.length() ? "Stored. Leave blank to keep it." : nullptr);
    body += field("x_rt", "Refresh token", "", "password", nullptr);
    body += field("x_acc", "Account id", settings.codexAccount, "text",
                  "All three come from <code>~/.codex/auth.json</code> under <code>tokens</code>.");
    body += "</fieldset>";

    body += "<fieldset><legend>Appearance</legend><label><span>Theme</span><select name=\"theme\">";
    for (uint8_t i = 0; i < 3; i++)
        body += "<option value=\"" + String(i) + "\"" + (settings.theme == i ? " selected" : "") + ">" +
                uiThemeName(i) + "</option>";
    body += "</select></label><p class=\"note\">Paper is black on white with red accents, Ink inverts it, "
            "Mono drops the red.</p></fieldset>";

    body += "<fieldset><legend>Cards</legend><p class=\"note\" style=\"margin:0 0 .9rem\">The first page shows up to three rows, in this order.</p>";
    body += check("k_s5", "Claude 5-hour session", settings.cardSession);
    body += check("k_op", "Opus weekly", settings.cardOpus);
    body += check("k_fa", "Fable weekly", settings.cardFable);
    body += check("k_cx", "Codex session", settings.cardCodex);
    body += check("k_xw", "Codex week, inline on the Codex row", settings.cardCodexWeek);
    body += check("k_cd", "Second page: Claude session, week and Sonnet", settings.cardClaudeDetail);
    body += check("k_in", "Indoor temperature and humidity in the footer", settings.cardIndoor);
    body += check("k_bt", "Battery voltage in the footer", settings.cardBattery);
    body += "</fieldset>";

    body += FPSTR(PAGE_TAIL);
    server.send(200, "text/html; charset=utf-8", body);
}

static String arg(const char *name) {
    String value = server.arg(name);
    value.trim();
    return value;
}

static void keepOrSet(String &target, const char *name) {
    String value = arg(name);
    if (value.length()) target = value;
}

static void handleSave() {
    keepOrSet(settings.wifiSsid, "ssid");
    keepOrSet(settings.wifiPass, "pass");
    keepOrSet(settings.proxyHost, "phost");
    keepOrSet(settings.tz, "tz");
    if (arg("pport").length()) settings.proxyPort = arg("pport").toInt();
    if (arg("every").length()) settings.refreshMinutes = arg("every").toInt();
    if (arg("theme").length()) settings.theme = arg("theme").toInt();
    if (arg("src").length()) settings.source = arg("src").toInt() ? SOURCE_PROXY : SOURCE_MAC;

    bool claudeChanged = arg("c_at").length() || arg("c_rt").length();
    keepOrSet(settings.claudeAccess, "c_at");
    keepOrSet(settings.claudeRefresh, "c_rt");
    if (claudeChanged) {
        settings.claudeExpiry = 0;
        settings.saveClaudeTokens();
    }

    bool codexChanged = arg("x_at").length() || arg("x_rt").length() || arg("x_acc").length();
    keepOrSet(settings.codexAccess, "x_at");
    keepOrSet(settings.codexRefresh, "x_rt");
    keepOrSet(settings.codexAccount, "x_acc");
    if (codexChanged) {
        settings.codexExpiry = 0;
        settings.saveCodexTokens();
    }

    settings.cardSession = server.hasArg("k_s5");
    settings.cardOpus = server.hasArg("k_op");
    settings.cardFable = server.hasArg("k_fa");
    settings.cardCodex = server.hasArg("k_cx");
    settings.cardCodexWeek = server.hasArg("k_xw");
    settings.cardClaudeDetail = server.hasArg("k_cd");
    settings.cardIndoor = server.hasArg("k_in");
    settings.cardBattery = server.hasArg("k_bt");
    settings.save();

    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><meta charset=\"utf-8\"><body style=\"font:15px/1.5 -apple-system,sans-serif;"
                "max-width:30rem;margin:4rem auto;padding:0 1.25rem;color:#1a1a1a\">"
                "<h1 style=\"font-size:1.2rem;font-weight:600\">Saved</h1>"
                "<p style=\"color:#6b6b6b\">The board is restarting and will join the network on its own.</p>");
    delay(600);
    ESP.restart();
}

void portalStart() {
    if (running) return;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(400);
    IPAddress ip = WiFi.softAPIP();
    dns.start(53, "*", ip);
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRoot);
    server.begin();
    running = true;
    log_i("portal up: ssid=%s ip=%s", AP_SSID, ip.toString().c_str());
    uiPortalScreen((String(AP_SSID) + " / " + AP_PASS).c_str(), ip.toString().c_str());
}

void portalLoop() {
    if (!running) return;
    dns.processNextRequest();
    server.handleClient();
}

void portalStop() {
    if (!running) return;
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    running = false;
}

bool portalRunning() { return running; }

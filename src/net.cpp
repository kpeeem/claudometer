#include "net.h"

#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <time.h>

#include "secrets.h"
#include "settings.h"

static bool joinWifi(const String &ssid, uint32_t timeoutMs) {
    WiFi.begin(ssid.c_str(), settings.wifiPass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(120);
    return WiFi.status() == WL_CONNECTED;
}

static String scanForSsid(const String &wanted) {
    int found = WiFi.scanNetworks();
    for (int i = 0; i < found; i++)
        if (WiFi.SSID(i).equalsIgnoreCase(wanted)) return WiFi.SSID(i);
    return String();
}

bool netConnectWifi(uint32_t timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(true);

    if (!joinWifi(settings.wifiSsid, timeoutMs)) {
        WiFi.disconnect(true);
        String exact = scanForSsid(settings.wifiSsid);
        if (!exact.length()) {
            log_w("no 2.4 GHz AP named '%s' in range", settings.wifiSsid.c_str());
            return false;
        }
        log_w("joining '%s' instead of '%s'", exact.c_str(), settings.wifiSsid.c_str());
        if (!joinWifi(exact, timeoutMs)) return false;
        settings.wifiSsid = exact;
        settings.save();
    }

    log_i("wifi ok ssid=%s ip=%s rssi=%d", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    MDNS.begin("quotaboard");
    netStartServices();
    return true;
}

bool netSyncClock(uint32_t timeoutMs) {
    if (time(nullptr) > 1760000000) return true;
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    uint32_t start = millis();
    while (time(nullptr) < 1760000000 && millis() - start < timeoutMs) delay(200);
    return time(nullptr) > 1760000000;
}

static bool resolveHost(const String &name, IPAddress &out) {
    if (out.fromString(name)) return true;
    if (name.endsWith(".local")) {
        String base = name.substring(0, name.length() - 6);
        IPAddress found = MDNS.queryHost(base, 4000);
        if (found != IPAddress((uint32_t)0)) {
            out = found;
            return true;
        }
    }
    return WiFi.hostByName(name.c_str(), out) == 1;
}

static int openProxySocket(const char *targetHost) {
    IPAddress proxy;
    if (!resolveHost(settings.proxyHost, proxy)) {
        log_e("cannot resolve proxy %s", settings.proxyHost.c_str());
        return -1;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    timeval tv{.tv_sec = 15, .tv_usec = 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(settings.proxyPort);
    addr.sin_addr.s_addr = (uint32_t)proxy;
    if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
        log_e("proxy %s:%u unreachable", proxy.toString().c_str(), settings.proxyPort);
        ::close(fd);
        return -1;
    }

    char req[192];
    int n = snprintf(req, sizeof(req),
                     "CONNECT %s:443 HTTP/1.1\r\nHost: %s:443\r\nProxy-Connection: keep-alive\r\n\r\n",
                     targetHost, targetHost);
    if (::send(fd, req, n, 0) != n) {
        ::close(fd);
        return -1;
    }

    char head[320];
    size_t have = 0;
    while (have < sizeof(head) - 1) {
        int got = ::recv(fd, head + have, 1, 0);
        if (got <= 0) break;
        have += got;
        head[have] = 0;
        if (have >= 4 && strcmp(head + have - 4, "\r\n\r\n") == 0) break;
    }
    head[have] = 0;
    int code = 0;
    if (sscanf(head, "HTTP/%*d.%*d %d", &code) != 1 || code != 200) {
        log_e("proxy CONNECT refused: %.60s", head);
        ::close(fd);
        return -1;
    }
    return fd;
}

struct TlsSession {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_net_context net;
    bool up = false;

    bool begin(int fd, const char *host) {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_net_init(&net);
        net.fd = fd;
        up = true;

        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"quotaboard", 10) != 0)
            return false;
        if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0)
            return false;
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        if (esp_crt_bundle_attach(&conf) != ESP_OK) return false;
        if (mbedtls_ssl_setup(&ssl, &conf) != 0) return false;
        if (mbedtls_ssl_set_hostname(&ssl, host) != 0) return false;
        mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, nullptr);

        int rc;
        while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                log_e("tls handshake with %s failed: -0x%04x", host, -rc);
                return false;
            }
        }
        uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
        if (flags != 0) {
            log_e("tls cert for %s rejected: 0x%08x", host, flags);
            return false;
        }
        return true;
    }

    bool writeAll(const char *data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            int rc = mbedtls_ssl_write(&ssl, (const unsigned char *)data + sent, len - sent);
            if (rc > 0) {
                sent += rc;
                continue;
            }
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            return false;
        }
        return true;
    }

    int read(unsigned char *buf, size_t len) {
        int rc;
        do {
            rc = mbedtls_ssl_read(&ssl, buf, len);
        } while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
        if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        return rc;
    }

    void end() {
        if (!up) return;
        mbedtls_ssl_close_notify(&ssl);
        if (net.fd >= 0) ::close(net.fd);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
        up = false;
    }
};

static String decodeChunked(const String &raw) {
    String out;
    out.reserve(raw.length());
    int at = 0;
    while (at < (int)raw.length()) {
        int eol = raw.indexOf("\r\n", at);
        if (eol < 0) break;
        long size = strtol(raw.substring(at, eol).c_str(), nullptr, 16);
        if (size <= 0) break;
        int start = eol + 2;
        if (start + size > (long)raw.length()) size = raw.length() - start;
        out += raw.substring(start, start + size);
        at = start + size + 2;
    }
    return out;
}

HttpResult netRequest(const char *host, const char *method, const char *path, const String &headers,
                      const String &body, size_t bodyLimit) {
    HttpResult result;

    int fd = openProxySocket(host);
    if (fd < 0) {
        result.error = "proxy";
        return result;
    }

    TlsSession tls;
    if (!tls.begin(fd, host)) {
        tls.end();
        result.error = "tls";
        return result;
    }

    String request;
    request.reserve(512 + headers.length() + body.length());
    request += String(method) + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + String(host) + "\r\n";
    request += "Connection: close\r\n";
    request += "Accept-Encoding: identity\r\n";
    request += headers;
    if (body.length()) request += "Content-Length: " + String(body.length()) + "\r\n";
    request += "\r\n";
    request += body;

    if (!tls.writeAll(request.c_str(), request.length())) {
        tls.end();
        result.error = "send";
        return result;
    }

    String raw;
    raw.reserve(4096);
    unsigned char buf[1024];
    size_t cap = bodyLimit + 4096;
    while (raw.length() < cap) {
        int got = tls.read(buf, sizeof(buf));
        if (got <= 0) break;
        raw.concat((const char *)buf, got);
    }
    tls.end();

    if (!raw.length()) {
        result.error = "empty";
        return result;
    }

    int split = raw.indexOf("\r\n\r\n");
    String head = split < 0 ? raw : raw.substring(0, split);
    String payload = split < 0 ? String() : raw.substring(split + 4);
    sscanf(head.c_str(), "HTTP/%*d.%*d %d", &result.status);

    String lower = head;
    lower.toLowerCase();
    if (lower.indexOf("transfer-encoding: chunked") >= 0) payload = decodeChunked(payload);

    result.body = payload;
    log_i("%s %s -> %d (%u bytes)", method, host, result.status, (unsigned)payload.length());
    return result;
}

static String exitLabel = "";

void netProbeExit() {
    HttpResult response = netRequest("ipinfo.io", "GET", "/json", "Accept: application/json\r\n", "", 2048);
    if (!response.ok()) {
        exitLabel = response.status == 0 ? "proxy down" : "exit ?";
        log_w("exit probe failed: status=%d err=%s", response.status, response.error.c_str());
        return;
    }
    int at = response.body.indexOf("\"country\"");
    int quote = at < 0 ? -1 : response.body.indexOf('"', response.body.indexOf(':', at));
    exitLabel = quote < 0 ? "exit ?" : "via " + response.body.substring(quote + 1, quote + 3);
    log_i("proxy exit: %s", exitLabel.c_str());
}

String netExitLabel() { return exitLabel; }

static bool otaUp = false;
static WebServer *panel = nullptr;

static void sendPanel() {
    Page page = panel->arg("page") == "1" ? Page::ClaudeDetail : Page::Overview;
    uiRenderPlanes(page);
    size_t bytes = uiPlaneBytes();
    panel->setContentLength(bytes * 2);
    panel->send(200, "application/octet-stream", "");
    panel->sendContent((const char *)uiPlane(false), bytes);
    panel->sendContent((const char *)uiPlane(true), bytes);
}

void netStartServices() {
    if (panel) return;
    if (*OTA_PASSWORD) {
        ArduinoOTA.setHostname("quotaboard");
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.onStart([] { log_i("ota start"); });
        ArduinoOTA.onEnd([] { log_i("ota done"); });
        ArduinoOTA.onError([](ota_error_t error) { log_e("ota error %u", error); });
        ArduinoOTA.begin();
        otaUp = true;
    }

    panel = new WebServer(80);
    panel->on("/panel.bin", sendPanel);
    panel->begin();
    log_i("/panel.bin ready on quotaboard.local, ota %s", otaUp ? "on" : "off without a password");
}

void netPollOta() {
    if (otaUp) ArduinoOTA.handle();
}

void netPollServices() {
    netPollOta();
    if (panel) panel->handleClient();
}

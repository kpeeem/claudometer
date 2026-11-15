#!/bin/zsh
set -euo pipefail

out="${0:a:h}/../src/secrets.h"
ssid="${WIFI_SSID:-Network}"
service="${WIFI_KEYCHAIN_SERVICE:-wifi-network-home}"
proxy_host="${PROXY_HOST:-proxy.local}"
proxy_port="${PROXY_PORT:-8080}"

ota_service="${OTA_KEYCHAIN_SERVICE:-quotaboard-ota}"
ota_pass="$(security find-generic-password -s "$ota_service" -w 2>/dev/null || true)"

if ! pass="$(security find-generic-password -s "$service" -w 2>/dev/null)"; then
    print -u2 "gen-secrets: no keychain item for service '$service'"
    print -u2 "  security add-generic-password -U -a '$ssid' -s '$service' -w '<password>'"
    exit 1
fi

escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }

cat > "$out" <<EOF
#pragma once

#define DEFAULT_WIFI_SSID "$(escape "$ssid")"
#define DEFAULT_WIFI_PASS "$(escape "$pass")"
#define DEFAULT_PROXY_HOST "$(escape "$proxy_host")"
#define DEFAULT_PROXY_PORT $proxy_port
#define OTA_PASSWORD "$(escape "$ota_pass")"
EOF

chmod 600 "$out"
print "gen-secrets: wrote src/secrets.h for SSID '$ssid' via $proxy_host:$proxy_port"

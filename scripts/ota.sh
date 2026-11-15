#!/bin/zsh
set -euo pipefail

here="${0:a:h}"
env_name="${1:-quotaboard}"
host="${OTA_HOST:-quotaboard.local}"

pass="$(security find-generic-password -s "${OTA_KEYCHAIN_SERVICE:-quotaboard-ota}" -w)"
exec "$HOME/.platformio/penv/bin/pio" run -e "$env_name" -t upload \
    --upload-port "$host" \
    --project-dir "$here/.." \
    -- --upload-flags "--auth=$pass"

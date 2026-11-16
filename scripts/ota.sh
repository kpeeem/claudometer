#!/bin/zsh
set -euo pipefail

here="$(cd -- "$(dirname -- "$0")" && pwd)"
env_name="${1:-quotaboard-ota}"
host="${OTA_HOST:-quotaboard.local}"

export OTA_PASSWORD="$(security find-generic-password -s "${OTA_KEYCHAIN_SERVICE:-quotaboard-ota}" -w)"
exec "$HOME/.platformio/penv/bin/pio" run -e "$env_name" -t upload \
    --upload-port "$host" \
    --project-dir "$here/.."

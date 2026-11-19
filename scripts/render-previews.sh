#!/bin/zsh
set -euo pipefail

root="${0:a:h:h}"
libs="$root/.pio/libdeps/quotaboard"
if [[ ! -d "$libs/U8g2_for_Adafruit_GFX" ]]; then
    "${PIO:-$HOME/.platformio/penv/bin/pio}" pkg install -e quotaboard -d "$root"
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

gfx="$libs/Adafruit GFX Library"
u8g2="$libs/U8g2_for_Adafruit_GFX/src"
clang -c -O1 -I"$u8g2" "$u8g2/u8g2_fonts.c" -o "$work/u8g2_fonts.o"
clang++ -std=c++20 -O1 -DARDUINO=100 -DUI_MOCK=1 -DPANEL_3C=1 \
    -I"$root/tools/preview/shim" -I"$root/src" -I"$gfx" -I"$u8g2" \
    "$root/tools/preview/render.cpp" "$root/src/ui.cpp" "$gfx/Adafruit_GFX.cpp" \
    "$u8g2/U8g2_for_Adafruit_GFX.cpp" "$work/u8g2_fonts.o" -o "$work/render"

"$work/render" "$work"
for bin in "$work"/*.bin; do
    python3 -I "$root/scripts/preview.py" "$root/preview/${${bin:t}%.bin}.png" "$bin"
done

# Quota board

Claude and Codex quota on a 4.2" e-paper board on your desk. Opus, Fable and Codex by default,
one row each, no laptop needed to read it.

Hardware is the [RockBase NM-EPD-420](https://wiki.rockbaseiot.com/docs/products/nm-epd-420/):
ESP32-S3R8, 400×300 e-paper, two buttons. All provider traffic goes out through an HTTP proxy on
the LAN, so the board never talks to Anthropic or OpenAI from a Russian IP.

![Overview](preview/overview.png)

## Quick start

```bash
git clone git@github.com:kpeeem/nm-epd-420-quota.git && cd nm-epd-420-quota
```

Put the Wi-Fi password in the login keychain once, then generate `src/secrets.h` from it:

```bash
security add-generic-password -U -a 'Network' -s wifi-network-home -w '<wifi-password>'
```

```bash
./scripts/gen-secrets.sh
```

Plug the board in over USB and flash it:

```bash
~/.platformio/penv/bin/pio run -e quotaboard -t upload
```

The board joins Wi-Fi and shows three empty rows. Hold **BOOT** for two seconds to open the setup
portal, join the `Quota-Setup` network from your phone or laptop, open `http://192.168.4.1`, paste
the tokens, press Save. It restarts and starts drawing numbers.

After the first flash you never need the cable again:

```bash
./scripts/ota.sh
```

## What is on screen

| | |
|---|---|
| **Paper** — default | **Ink** |
| ![Paper](preview/overview.png) | ![Ink](preview/theme-ink.png) |
| **Mono** — no red | **Second page** |
| ![Mono](preview/theme-mono.png) | ![Detail](preview/detail.png) |

Each row is one quota window: the big number and the bar are the window you care about, the line
underneath says which window it is and when it resets. The Opus row carries the 5-hour session
number on the right, the Codex row carries its weekly number.

A row turns red at 90% — the bar and a chip behind the number. That is the only thing red means.

The footer shows when the numbers were last fetched, the proxy exit country (`via DE`), optional
indoor temperature and battery, and page dots when a second page is enabled.

### Buttons

| Button | Press | Does |
|---|---|---|
| USER | tap | next page |
| USER | hold | fetch now |
| BOOT | tap | help screen |
| BOOT | hold 2s | setup portal |

![Help](preview/help.png)

### Setup portal

![Portal](preview/portal.png)

Wi-Fi, proxy, timezone, refresh interval, theme, tokens and which cards appear. Tokens are written
to the board's NVS and never echoed back to the browser — leave a token field blank to keep what is
already stored.

## Where the numbers come from

| Provider | Request | Token |
|---|---|---|
| Claude | `GET api.anthropic.com/api/oauth/usage` with `anthropic-beta: oauth-2025-04-20` | `~/.claude/.credentials.json`, or the login keychain item `Claude Code-credentials` |
| Codex | `GET chatgpt.com/backend-api/wham/usage` | `~/.codex/auth.json` → `tokens` |

Both are the same undocumented endpoints the official clients use, and both access tokens expire.
The board holds the refresh token too and renews against
`console.anthropic.com/v1/oauth/token` and `auth.openai.com/oauth/token`. Both providers **rotate**
refresh tokens, so the board writes the new one back to NVS on every renewal.

Two things to know before relying on this:

- `/api/oauth/usage` rate-limits hard — roughly a handful of requests per access token before it
  starts answering 429 ([claude-code#31637](https://github.com/anthropics/claude-code/issues/31637)).
  The default 20-minute interval is deliberate. On a 429 the board keeps the last good numbers and
  marks them `cached` rather than blanking the row.
- Refreshing with the official client id from an unusual network path can trip Cloudflare
  ([claude-code#47754](https://github.com/anthropics/claude-code/issues/47754)). If Claude rows go
  to `sign in again`, re-paste a fresh token in the portal.

## Networking

Everything goes through an HTTP `CONNECT` proxy — the one set in the portal. The board opens a socket to the proxy, issues `CONNECT host:443`,
then runs a full mbedTLS handshake over that socket with certificate-bundle verification. The exit
country is probed once per cycle and shown in the footer, so a silently broken tunnel is visible
rather than guessed at.

Routers often publish the 2.4 GHz and 5 GHz bands under names that differ only in case. The radio is 2.4 GHz only,
so on a failed join the board rescans and matches the SSID case-insensitively.

## Builds

| Env | Panel | Refresh |
|---|---|---|
| `quotaboard` | tri-colour GDEY042Z98 | full only, ~25 s |
| `quotaboard-bw` | black/white GYE042A87 | fast partial, ~0.4 s |
| `quotaboard-ota` | same as `quotaboard`, uploads over the air | |

The two panels are visually identical from the outside and share a controller. If the display never
shows red, build `quotaboard-bw` — the same UI draws red as black and the per-minute clock update
switches on.

## Design on 1-bit

The panel has no grey, so grey is a 4×4 Bayer dither: bar tracks at ~19%, hairlines at ~37%. Text
is never dithered, and nothing is drawn on top of a dithered area — at this resolution a halftone
behind a glyph turns it to mush.

Type is U8g2 bitmap fonts, which are hinted at their design size and render crisply with no
antialiasing: `logisoso30` for the numbers, `helvB14`/`helvB10`/`helvB08`/`helvR08` for everything
else. The alert chip is sized from the font's real ascent rather than a guessed constant.

Layout is fixed geometry with right-aligned numbers, so values changing width never move anything.
The same `drawPage` runs for full and partial refreshes, which is what keeps partial updates
pixel-identical to a full redraw.

### Seeing the UI without a panel

A debug build renders into an off-screen two-plane canvas and dumps it over serial, which the host
turns into a PNG — every screenshot above was produced this way:

```bash
PLATFORMIO_BUILD_FLAGS="-DUI_MOCK=1" ~/.platformio/penv/bin/pio run -e quotaboard -t upload
```

```bash
python3 -I scripts/preview.py preview/overview.png m0d 40
```

The argument after the filename is a key sequence sent over serial: `m` mock data, `0`/`1`/`2`
theme, `p` next page, `d` dump page, `h` dump help, `s` dump portal.

## Layout of the source

| File | |
|---|---|
| `src/board.h` | pin map, from the vendor's `config.h` |
| `src/net.cpp` | Wi-Fi, proxy CONNECT, mbedTLS, OTA, exit probe |
| `src/quota.cpp` | provider clients, OAuth refresh, response parsing |
| `src/ui.cpp` | themes, layout, dither, screens, preview dump |
| `src/portal.cpp` | setup portal |
| `src/settings.cpp` | NVS |

## Secrets

`src/secrets.h` is generated from the login keychain and is gitignored; `src/secrets.h.example`
shows its shape. Provider tokens are never in the repo or in the build — they only ever reach the
board through the setup portal.

#pragma once

#include <Arduino.h>
#include <time.h>

enum class Health { NoToken, Ready, Stale, Expired, Throttled, NetError, BadData, Waiting };

struct Window {
    bool valid = false;
    float used = 0;
    time_t resetAt = 0;
};

struct Provider {
    const char *name = "";
    Health health = Health::NoToken;
    String plan;
    Window session;
    Window week;
    Window opus;
    Window sonnet;
    Window fable;
    Window opusSession;
    time_t observedAt = 0;

    bool hasData() const { return session.valid || week.valid || opus.valid || sonnet.valid || fable.valid; }
};

extern Provider claudeState;
extern Provider codexState;

void quotaInit();
void quotaRefreshAll();
const char *healthText(Health health);

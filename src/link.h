#pragma once

#include <Arduino.h>

enum class LinkEvent : uint8_t { None, Data, Settings };

struct Agent {
    String state;
    String project;
    uint8_t count = 0;
};

extern Agent agentState;

void linkStart();
LinkEvent linkPoll();
bool linkConnected();

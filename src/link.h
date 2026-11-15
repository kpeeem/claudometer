#pragma once

#include <Arduino.h>

enum class LinkEvent : uint8_t { None, Data, Settings };

void linkStart();
LinkEvent linkPoll();
bool linkConnected();

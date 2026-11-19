#pragma once

#include "Arduino.h"

#define WL_CONNECTED 3

struct IPAddress {
    String toString() const { return "192.168.1.42"; }
};

class WiFiClass {
  public:
    int status() const { return WL_CONNECTED; }
    int RSSI() const { return -56; }
    String SSID() const { return "HomeWiFi"; }
    IPAddress localIP() const { return {}; }
};

extern WiFiClass WiFi;

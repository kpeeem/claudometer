#pragma once

#include <Adafruit_GFX.h>

#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF
#define GxEPD_RED 0xF800

struct GxEPD2_420c_GDEY042Z98 {
    static const int16_t WIDTH = 400;
    static const int16_t HEIGHT = 300;
    GxEPD2_420c_GDEY042Z98(int, int, int, int) {}
    void setBusyCallback(void (*)(const void *)) {}
};

template <typename Driver, int16_t PageHeight>
class GxEPD2_3C : public Adafruit_GFX {
  public:
    explicit GxEPD2_3C(Driver driver) : Adafruit_GFX(Driver::WIDTH, Driver::HEIGHT), epd2(driver) {}
    Driver epd2;
    void init(uint32_t, bool, uint16_t, bool) {}
    void setFullWindow() {}
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) {}
    void firstPage() {}
    bool nextPage() { return false; }
    void hibernate() {}
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};

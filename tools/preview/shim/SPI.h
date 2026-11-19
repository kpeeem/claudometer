#pragma once

class SPIClass {
  public:
    void begin(int, int, int, int) {}
    void end() {}
};

extern SPIClass SPI;

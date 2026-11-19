#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <string>

#define PROGMEM
#define radians(degrees) ((degrees) * M_PI / 180.0)

class __FlashStringHelper;
#define log_i(...)
#define log_w(...)
#define log_e(...)

using std::max;
using std::min;

template <typename T, typename L, typename H>
T constrain(T value, L low, H high) {
    return value < low ? low : (value > high ? high : value);
}

inline void delay(unsigned long) {}
inline unsigned long millis() { return 0; }

class String {
  public:
    String() = default;
    String(const char *text) : value(text ? text : "") {}
    String(const std::string &text) : value(text) {}
    String(char c) : value(1, c) {}
    String(int number) : value(std::to_string(number)) {}
    String(unsigned number) : value(std::to_string(number)) {}
    String(long number) : value(std::to_string(number)) {}
    String(unsigned long number) : value(std::to_string(number)) {}
    String(float number, unsigned char decimals = 2) : String((double)number, decimals) {}
    String(double number, unsigned char decimals = 2) {
        char out[32];
        snprintf(out, sizeof(out), "%.*f", decimals, number);
        value = out;
    }

    const char *c_str() const { return value.c_str(); }
    unsigned length() const { return value.size(); }
    String &operator+=(const String &other) {
        value += other.value;
        return *this;
    }
    bool operator==(const char *other) const { return value == other; }
    bool operator==(const String &other) const { return value == other.value; }
    friend String operator+(const String &a, const String &b) { return String(a.value + b.value); }
    friend String operator+(const String &a, const char *b) { return String(a.value + b); }
    friend String operator+(const char *a, const String &b) { return String(a + b.value); }

  private:
    std::string value;
};

class Print {
  public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t written = 0;
        while (size--) written += write(*buffer++);
        return written;
    }
    size_t write(const char *text) { return text ? write((const uint8_t *)text, strlen(text)) : 0; }
    size_t print(const char *text) { return write(text); }
    size_t print(const String &text) { return write(text.c_str()); }
    size_t println(const char *text) { return print(text) + print("\n"); }
};

class HardwareSerial : public Print {
  public:
    size_t write(uint8_t) override { return 1; }
};

extern HardwareSerial Serial;

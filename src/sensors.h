#pragma once

#include <Arduino.h>

void sensorsInit();
void sensorsRead();
bool sensorsIndoorValid();
float sensorsTemperature();
float sensorsHumidity();
float sensorsBatteryVolts();

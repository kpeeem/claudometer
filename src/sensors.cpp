#include "sensors.h"

#include <Adafruit_AHTX0.h>
#include <Wire.h>

#include "board.h"
#include "settings.h"

static Adafruit_AHTX0 aht;
static bool ahtReady = false;
static bool indoorValid = false;
static float temperature = 0;
static float humidity = 0;
static float battery = 0;

void sensorsInit() {
    pinMode(PIN_TEMP_CTL, OUTPUT);
    digitalWrite(PIN_TEMP_CTL, HIGH);
    pinMode(PIN_ADC_EN, OUTPUT);
    digitalWrite(PIN_ADC_EN, LOW);
    delay(20);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    ahtReady = aht.begin(&Wire);
    if (!ahtReady) log_w("AHT20 not found on I2C");
}

static void readBattery() {
    digitalWrite(PIN_ADC_EN, HIGH);
    delay(80);
    uint32_t total = 0;
    for (int i = 0; i < 24; i++) {
        total += analogReadMilliVolts(PIN_BATT_ADC);
        delay(2);
    }
    digitalWrite(PIN_ADC_EN, LOW);
    battery = (total / 24.0f) * BATT_ADC_DIV / 1000.0f;
}

void sensorsRead() {
    if (settings.cardIndoor && ahtReady) {
        sensors_event_t humidityEvent, temperatureEvent;
        if (aht.getEvent(&humidityEvent, &temperatureEvent)) {
            temperature = temperatureEvent.temperature;
            humidity = humidityEvent.relative_humidity;
            indoorValid = true;
        }
    }
    if (settings.cardBattery) readBattery();
}

bool sensorsIndoorValid() { return indoorValid; }
float sensorsTemperature() { return temperature; }
float sensorsHumidity() { return humidity; }
float sensorsBatteryVolts() { return battery; }

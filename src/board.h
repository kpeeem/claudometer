#pragma once

#if PANEL_3C
#define BOARD_NAME "NM-EPD-420"
#else
#define BOARD_NAME "NM-EPD-420-BW"
#endif

#define FW_VERSION "1.0.0"

#define PIN_EPD_SCK 2
#define PIN_EPD_MOSI 1
#define PIN_EPD_MISO -1
#define PIN_EPD_CS 46
#define PIN_EPD_DC 4
#define PIN_EPD_RST 5
#define PIN_EPD_BUSY 6

#define PIN_I2C_SDA 39
#define PIN_I2C_SCL 38
#define PIN_TEMP_CTL 40

#define PIN_BOOT_BTN 0
#define PIN_USER_BTN 45

#define PIN_LORA_EN 47
#define PIN_CODEC_EN 44
#define PIN_ADC_EN 43

#define PIN_BATT_ADC 3
#define BATT_ADC_DIV 2

#define EPD_WIDTH 400
#define EPD_HEIGHT 300

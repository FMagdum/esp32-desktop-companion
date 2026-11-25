#pragma once
#include <Arduino.h>

// Waveshare ESP32-S3 Audio Board + 3.5inch Capacitive Touch LCD (FPC)
// Do NOT reuse the pinout of an ESP32-S3 DevKit + external SPI LCD.
namespace BoardPins {
constexpr int I2C_SDA = 11;
constexpr int I2C_SCL = 10;
constexpr uint8_t TCA9555_ADDR = 0x20;
constexpr uint8_t FT6336_ADDR = 0x38;

constexpr int LCD_CS   = 3;
constexpr int LCD_SCLK = 4;
constexpr int LCD_MOSI = 9;
constexpr int LCD_MISO = 8;
constexpr int LCD_DC   = 7;
constexpr int LCD_BL   = 5;

// Waveshare ESP32-S3-AUDIO-Board schematic: GPIO38 -> LED_DIN.
constexpr int LED_RING_DATA = 38;
constexpr uint16_t LED_RING_COUNT = 7;

// Phase 5: Waveshare Audio Board TF card and ES8311 speaker pin tables.
constexpr int SD_CLK = 40;
constexpr int SD_CMD = 42;
constexpr int SD_D0 = 41;
constexpr uint8_t SD_CS_MASK = 0x08; // EXIO3, keep HIGH for native SDMMC
constexpr uint8_t SPEAKER_ENABLE_MASK = 0x01; // port 1, EXIO8
constexpr int AUDIO_MCLK = 12;
constexpr int AUDIO_BCLK = 13;
constexpr int AUDIO_LRCK = 14;
constexpr int AUDIO_DIN = 15; // ES7210 -> ESP32, Waveshare MIC pin table
constexpr uint8_t ES7210_ADDR = 0x40;
constexpr int AUDIO_DOUT = 16; // ESP32 -> ES8311

constexpr int LCD_WIDTH  = 320;
constexpr int LCD_HEIGHT = 480;

constexpr uint8_t LCD_RST_MASK   = 0x01; // TCA9555 port 0, EXIO0
constexpr uint8_t TOUCH_RST_MASK = 0x02; // TCA9555 port 0, EXIO1
}

namespace RGB565 {
constexpr uint16_t BLACK  = 0x0000;
constexpr uint16_t WHITE  = 0xFFFF;
constexpr uint16_t CYAN   = 0x07FF;
constexpr uint16_t GREEN  = 0x07E0;
constexpr uint16_t RED    = 0xF800;
constexpr uint16_t YELLOW = 0xFFE0;
}




#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
namespace Display {
bool begin();
void backlight(bool enabled);
// 10..100%; retains the working backlight GPIO and never turns the UI invisible.
bool setBrightness(uint8_t percent);
uint8_t brightness();
Arduino_GFX *graphics();
}

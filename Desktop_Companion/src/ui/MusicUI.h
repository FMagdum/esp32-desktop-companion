#pragma once
#include <Arduino.h>
namespace MusicUI {
void begin();
void update(uint32_t now);
bool onPress(uint16_t x, uint16_t y);
}

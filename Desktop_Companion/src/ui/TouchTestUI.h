#pragma once
#include <Arduino.h>

namespace TouchTestUI {
void draw();
void showTouch(uint16_t x, uint16_t y);
void showTouchError();
}

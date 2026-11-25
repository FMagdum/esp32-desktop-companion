#pragma once
#include <stdint.h>
namespace AppUI {
void showStartup(const char *stage);
void begin(bool audioAvailable);
void update(uint32_t now);
// Poll even on release, so taps and swipes can be distinguished.
void touch(uint16_t x, uint16_t y, bool pressed, uint32_t now);
}

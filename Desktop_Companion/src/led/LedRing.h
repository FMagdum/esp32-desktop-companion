#pragma once
#include <Arduino.h>
#include "../face/Face.h"
namespace LedRing {
bool begin();
// Screens: Home/Music/Chat/Settings. Palette: auto/cyan/blue/violet/amber/green/off.
void setTheme(uint8_t screen, uint8_t palette);
void setRecording(bool recording);
void setSpeaking(bool speaking);
// Valid artwork overrides the Music page color. Off keeps its priority.
void setMusicAccent(uint16_t rgb565, bool available = true);
void update(uint32_t now, Face::Expression expression);
}

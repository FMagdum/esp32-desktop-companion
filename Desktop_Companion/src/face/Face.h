#pragma once
#include <Arduino.h>
namespace Face {
enum class Expression : uint8_t { Idle, Blink, Happy };
Expression expression();
bool begin();
void setVisible(bool visible);
// Recenter within the content area after the navigation rail changes width.
void setLayout(int contentWidth);
// Main/UI task: active only while desktop music is playing.
void setSinging(bool singing);
void setSleeping(bool sleeping);
void setVoiceActivity(bool listening, bool speaking);
void redraw();
void update(uint32_t now);
void onTouch(uint16_t x, uint16_t y, uint32_t now);
}

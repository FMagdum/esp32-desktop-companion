#include "LedRing.h"
#include "../config/BoardPins.h"
#include "../config/LedConfig.h"
namespace {
Adafruit_NeoPixel *pixels = nullptr;
uint32_t lastFrame = 0, effectStart = 0;
Face::Expression current = Face::Expression::Idle;
uint8_t screen = 0, palette = 0, previousScreen = 255, previousPalette = 255;
bool speaking = false, previousSpeaking = false, artworkAvailable = false;
bool recording = false, previousRecording = false, firstFrame = true;
uint16_t red = 0, green = 0, blue = 0;
uint16_t musicAccent = 0xFDB0;
uint16_t approach(uint16_t value, uint16_t target) {
  if (value < target) return min(uint16_t(value + 3), target);
  return value > target ? value - min(uint16_t(3), uint16_t(value - target)) : value;
}
constexpr uint8_t colors[][3] = {
  {50, 216, 255}, {65, 148, 255}, {174, 129, 255},
  {255, 178, 64}, {73, 235, 156}
};
constexpr uint8_t screenColors[] = {0, 3, 2, 1};
}
namespace LedRing {
bool begin() {
  static Adafruit_NeoPixel ring(BoardPins::LED_RING_COUNT,
                                BoardPins::LED_RING_DATA, LedConfig::PIXEL_TYPE);
  if (!ring.begin() || !ring.getPixels()) return false;
  pixels = &ring;
  pixels->clear();
  pixels->show();
  lastFrame = effectStart = millis();
  firstFrame = true;
  red = green = blue = 0;
  return true;
}
void setTheme(uint8_t page, uint8_t mode) { screen = min(uint8_t(3), page); palette = min(uint8_t(6), mode); }
void setMusicAccent(uint16_t color, bool available) { musicAccent = color; artworkAvailable = available; }
void setSpeaking(bool value) { speaking = value; }
void setRecording(bool value) { recording = value; }
void update(uint32_t now, Face::Expression expression) {
  if (!pixels) return;
  if (screen != 0) expression = Face::Expression::Idle;
  const bool changed = firstFrame || expression != current || screen != previousScreen ||
                       palette != previousPalette || recording != previousRecording || speaking != previousSpeaking;
  if (!changed && uint32_t(now - lastFrame) < 30) return;
  if (changed) effectStart = now;
  current = expression;
  previousScreen = screen;
  previousPalette = palette;
  previousRecording = recording;
  previousSpeaking = speaking;
  firstFrame = false;
  lastFrame = now;
  const uint32_t period = speaking ? 1200 : recording ? 900 : current == Face::Expression::Happy ? 900 : 3000;
  const uint32_t phase = uint32_t(now - effectStart) % period;
  const uint32_t triangle = phase < period / 2 ? phase : period - phase;
  uint16_t level = 8 + triangle * (LedConfig::MAX_BRIGHTNESS - 8) / (period / 2);
  if (current == Face::Expression::Blink) level = 2;
  uint8_t r, g, b;
  const uint8_t index = palette == 0 ? screenColors[screen] : min(uint8_t(4), uint8_t(palette - 1));
  r = colors[index][0]; g = colors[index][1]; b = colors[index][2];
  if (palette == 0 && screen == 0 && current == Face::Expression::Happy) { r = 73; g = 235; b = 156; }

  if (screen == 1 && artworkAvailable) {
    r = (musicAccent >> 11) * 255 / 31;
    g = ((musicAccent >> 5) & 63) * 255 / 63;
    b = (musicAccent & 31) * 255 / 31;
  }
  if (speaking) { r = 40; g = 235; b = 185; }
  if (recording) { r = 255; g = 32; b = 32; }
  if (palette == 6) level = 0; // Off remains off, including recording.
  red = approach(red, uint16_t(r) * level / 255);
  green = approach(green, uint16_t(g) * level / 255);
  blue = approach(blue, uint16_t(b) * level / 255);
  pixels->fill(pixels->Color(red, green, blue));
  pixels->show();
}
}

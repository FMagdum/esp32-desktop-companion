#include "MusicUI.h"
#include "../assistant/Assistant.h"
#include "../music/Music.h"
#include "../display/Display.h"
#include "../config/BoardPins.h"

namespace {
uint32_t lastRevision = UINT32_MAX;
uint32_t lastUpdate = 0;
uint32_t lastPress = 0;
bool firstPress = true;
bool lastAssistantVisible = false;

void textLine(const char *text, int x, int y, size_t skip, size_t count, uint16_t color) {
  auto *gfx = Display::graphics();
  gfx->setTextSize(2);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  size_t index = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p && index < skip + count; ++p) {
    // Built-in font is ASCII. Render one '?' per unsupported UTF-8 codepoint.
    if ((*p & 0xC0) == 0x80) continue;
    if (index++ < skip) continue;
    gfx->write(*p >= 32 && *p <= 126 ? *p : '?');
  }
}
void draw(const Music::Snapshot &state) {
  auto *gfx = Display::graphics();
  // Keep clear of the animated canvas at x20..299, y160..319.
  if (!CompanionAssistant::snapshot().visible) {
  gfx->fillRect(0, 0, BoardPins::LCD_WIDTH, 150, RGB565::BLACK);
  textLine(state.status, 8, 12, 0, 25, state.online ? RGB565::CYAN : RGB565::YELLOW);
  textLine(state.title, 8, 48, 0, 25, RGB565::WHITE);
  textLine(state.title, 8, 72, 25, 25, RGB565::WHITE);
  textLine(state.artist, 8, 108, 0, 25, RGB565::GREEN);
  }
  gfx->fillRect(0, 356, BoardPins::LCD_WIDTH, 124, RGB565::BLACK);
  const uint16_t color = state.online ? RGB565::CYAN : uint16_t(0x4208);
  const char *labels[] = {"PREV", "PLAY/PAUSE", "NEXT"};
  for (int i = 0; i < 3; ++i) {
    gfx->drawRoundRect(8 + i * 104, 360, 96, 60, 8, color);
    gfx->setTextSize(1);
    gfx->setTextColor(color);
    gfx->setCursor(56 + i * 104 - strlen(labels[i]) * 3, 387);
    gfx->print(labels[i]);
  }
  const char *playState = !state.online ? "Controls offline" :
                         state.trackState == 1 ? "Playing on desktop" :
                         state.trackState == 0 ? "Paused on desktop" :
                         state.trackState == 2 ? "Buffering on desktop" : "Player ready";
  textLine(playState, 8, 444, 0, 25, RGB565::WHITE);
}
}
namespace MusicUI {
void begin() {
  lastRevision = UINT32_MAX;
  update(millis());
}
void update(uint32_t now) {
  if (lastRevision != UINT32_MAX && uint32_t(now - lastUpdate) < 200) return;
  lastUpdate = now;
  const auto state = Music::snapshot();
  const bool assistantVisible = CompanionAssistant::snapshot().visible;
  if (state.revision == lastRevision && assistantVisible == lastAssistantVisible) return;
  lastAssistantVisible = assistantVisible;
  lastRevision = state.revision;
  draw(state);
}
bool onPress(uint16_t x, uint16_t y) {
  if (y < 360 || y >= 420) return false;
  int index = -1;
  for (int i = 0; i < 3; ++i)
    if (x >= 8 + i * 104 && x < 104 + i * 104) index = i;
  if (index < 0) return false;
  const uint32_t now = millis();
  if (!firstPress && uint32_t(now - lastPress) < 500) return true;
  firstPress = false;
  lastPress = now;
  const Music::Command commands[] = {Music::Command::Previous,
                                     Music::Command::PlayPause, Music::Command::Next};
  if (!Music::command(commands[index])) Serial.println("Music control unavailable");
  return true;
}
}


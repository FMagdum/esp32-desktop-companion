#include "AudioUI.h"
#include "../audio/Sound.h"
#include "../assistant/Assistant.h"
#include "../display/Display.h"
#include "../config/BoardPins.h"

namespace {
uint32_t revision = UINT32_MAX, assistantRevision = UINT32_MAX, lastDraw = 0, lastPress = 0;
bool firstPress = true;
void line(const char *text, int x, int y, size_t skip, size_t count, uint16_t color, uint8_t size=1) {
  auto *gfx = Display::graphics();
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x,y);
  size_t index=0;
  for (const unsigned char *p=(const unsigned char *)text; *p && index<skip+count; ++p) {
    if ((*p & 0xC0)==0x80) continue;
    if (index++<skip) continue;
    gfx->write(*p>=32 && *p<=126 ? *p : '?');
  }
}
void draw(const Sound::Snapshot &state, const CompanionAssistant::Snapshot &assistant) {
  auto *gfx = Display::graphics();
  gfx->fillRect(0, 320, 320, 36, RGB565::BLACK);
  const bool recording = state.route == Sound::Route::Recording;
  const bool busy = assistant.busy || Sound::assistantReserved();
  const uint16_t inactive=0x4208;
  const uint16_t recColor = recording ? RGB565::RED :
                           state.microphoneReady && !busy ? RGB565::CYAN : inactive;
  const uint16_t playColor = state.hasRecording && !recording && !busy ? RGB565::CYAN : inactive;
  const uint16_t askColor = assistant.configured && state.hasRecording && !recording && !busy ?
                           RGB565::GREEN : inactive;
  gfx->drawRoundRect(8,324,96,28,5,recColor);
  gfx->drawRoundRect(112,324,96,28,5,playColor);
  gfx->drawRoundRect(216,324,96,28,5,askColor);
  gfx->setTextSize(1);
  gfx->setTextColor(recColor);
  gfx->setCursor(16,334);
  if (recording) gfx->printf("STOP %lu.%lus", (unsigned long)(state.recordedMs/1000),
                            (unsigned long)((state.recordedMs%1000)/100));
  else gfx->print(state.microphoneReady ? "RECORD 5s" : "MIC OFF");
  line("PLAY LAST",125,334,0,12,playColor);
  line(busy ? "THINKING" : "ASK LAST",230,334,0,12,askColor);

  gfx->fillRect(8,152,144,5,uint16_t(0x2104));
  gfx->fillRect(168,152,144,5,uint16_t(0x2104));
  gfx->fillRect(8,152,uint32_t(state.peakLeft)*144/32768,5,
                state.peakLeft>=32000 ? RGB565::RED : RGB565::GREEN);
  gfx->fillRect(168,152,uint32_t(state.peakRight)*144/32768,5,
                state.peakRight>=32000 ? RGB565::RED : RGB565::GREEN);

  if (assistant.visible) {
    gfx->fillRect(0,0,320,150,RGB565::BLACK);
    line(assistant.status,8,12,0,25,RGB565::CYAN,2);
    line("You:",8,40,0,50,RGB565::GREEN);
    for (int i=0;i<2;++i) line(assistant.transcript,8,52+i*10,i*50,50,RGB565::WHITE);
    line("AI voice:",8,76,0,50,RGB565::GREEN);
    for (int i=0;i<6;++i) line(assistant.reply,8,88+i*9,i*50,50,RGB565::WHITE);
  }
}
}
namespace AudioUI {
void begin() { revision=assistantRevision=UINT32_MAX; update(millis()); }
void update(uint32_t now) {
  if (revision!=UINT32_MAX && uint32_t(now-lastDraw)<200) return;
  const auto state=Sound::snapshot();
  const auto assistant=CompanionAssistant::snapshot();
  if (state.revision==revision && assistant.revision==assistantRevision) return;
  revision=state.revision;
  assistantRevision=assistant.revision;
  lastDraw=now;
  draw(state,assistant);
}
bool onPress(uint16_t x,uint16_t y) {
  if (y<324 || y>=352) return false;
  int button=-1;
  for (int i=0;i<3;++i) if (x>=8+i*104 && x<104+i*104) button=i;
  if (button<0) return false;
  const uint32_t now=millis();
  if (!firstPress && uint32_t(now-lastPress)<500) return true;
  firstPress=false;
  lastPress=now;
  const auto assistant=CompanionAssistant::snapshot();
  if (assistant.busy) { Serial.println("Assistant busy"); return true; }
  bool accepted=false;
  if (button==0) {
    CompanionAssistant::clearText();
    accepted=Sound::toggleRecording();
  } else if (button==1) accepted=Sound::playRecording();
  else accepted=CompanionAssistant::askLast();
  if (!accepted) Serial.println("Audio/assistant control unavailable or busy");
  return true;
}
}

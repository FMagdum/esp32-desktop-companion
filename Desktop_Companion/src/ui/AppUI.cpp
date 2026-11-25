#include "AppUI.h"
#include "DeviceSettings.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "../config/UIConfig.h"
#include "../display/Display.h"
#include "../face/Face.h"
#include "../led/LedRing.h"
#include "../music/Music.h"
#include "../music/Artwork.h"
#include "../audio/Sound.h"
#include "../assistant/Assistant.h"
#include "../storage/Storage.h"
#include "../network/Network.h"
#include "../alarm/Alarms.h"
#include "../assistant/Conversation.h"
#include "ConversationStatus.h"
#include "HomeCaptions.h"
#include "../drivers/Buttons.h"
#include <WiFi.h>
#include <freertos/task.h>
#include <esp_arduino_version.h>
#include <stdlib.h>
#include <time.h>
#include <esp_sleep.h>
#include <math.h>
#include <string.h>

namespace {
using namespace UIConfig;
enum class Page : uint8_t { Home, Music, Chat, Settings, Alarms, Ringing };
enum class Font : uint8_t { Small, Body, Heading, Large };
Arduino_GFX *g = nullptr;
Page page = Page::Home;
Page beforeAlarms = Page::Home, beforeRinging = Page::Home;
Alarms::Snapshot alarmState;
bool alarmClockReady = false, alarmSchedulerRunning = false;
enum AlarmEditorRegion : uint8_t {
  HourUp = 1, HourDown = 2, MinuteUp = 4, MinuteDown = 8,
  HourValue = 16, MinuteValue = 32, AlarmDay = 64, AlarmPreview = 128
};
uint8_t alarmEditorDirty = 0;
struct AlarmStep { int x, y; uint8_t buttonRegion, valueRegion; int delta; };
constexpr AlarmStep AlarmSteps[] = {
  {141,112,HourUp,HourValue,1}, {141,173,HourDown,HourValue,-1},
  {283,112,MinuteUp,MinuteValue,1}, {283,173,MinuteDown,MinuteValue,-1}
};
uint32_t lastAlarmPoll = 0;
constexpr int ClockW = 78;
constexpr int AlarmRows = 4, AlarmListY = 83, AlarmRowStep = 44, AlarmRowHeight = 41;
constexpr int AlarmActionY = 266;
int alarmListPage = 0, alarmSlot = Alarms::NoSlot, alarmHour = 7, alarmMinute = 0;
bool editingAlarm = false, alarmTomorrow = false;
constexpr int SHORTCUT_SIZE=52, SHORTCUT_Y=260, HOME_X=420, LEFT_SHORTCUT_X=8;
constexpr int SETTINGS_X=430, SETTINGS_W=44;
constexpr int CHAT_CONTROL_W=196, CHAT_TALK_X=12, CHAT_TEACH_X=216;
constexpr int contentWidth() { return WIDTH; }
constexpr int settingsShift() { return 32; }
uint8_t category = 0;
bool audioReady = false, dirty = true, headerDirty = true;
bool touching = false, moved = false, sliderDrag = false, chatDrag = false;
bool chatRegionDirty = false, sliderRegionDirty = false;
bool musicArtworkDirty = false, musicDetailsDirty = false, musicProgressDirty = false;
uint8_t musicButtonsDirty = 0;
bool musicVoiceDirty = false;
bool chatStateDirty = false, chatTimerDirty = false, chatScrollDirty = false;
bool homeTextDirty = false;
uint8_t chatControlsDirty = 0;
uint32_t seenMusicNavigation = 0;
int greetingHour = -2, aiIndicatorState = -1;
RemoteSettings::Status remoteStatus;
uint32_t startupUntil=0, shutdownAt=0, lastActivity=0;
bool homeCanceling=false;
Conversation conversation;
bool promptQueued = false, replyPlaybackPending = false;
uint32_t noSpeechAt=0, canceledWake=0;
constexpr uint32_t HOME_SLEEP_MS=300000, NO_SPEECH_NOTICE_MS=2400;
uint32_t seenChatNavigation=0;
uint32_t seenWake = 0;
uint16_t startX = 0, startY = 0, previousY = 0, fingerX = 0, fingerY = 0;
int flashTransport = -1, dotPhase = -1;
uint32_t flashAt = 0, progressSecond = UINT32_MAX;
uint16_t *artworkPixels = nullptr;
uint32_t copiedArtwork = UINT32_MAX;
MusicArtwork::Snapshot artwork;
int dragRemainder = 0;
uint32_t lastPoll = 0, lastInfo = 0, noticeAt = 0;
char notice[64] = {};
char headerKey[80] = {};
bool clockStarted = false, wasConnected = false;
Music::Snapshot music;
Sound::Snapshot audio;
CompanionAssistant::Snapshot assistant;
String ssid, address;
int signalDb = 0;


// Session-only history. Prefer PSRAM, with a small fallback when unavailable.
struct Turn { char question[1024] = {}, answer[1024] = {}; };
struct ChatLine { char text[64]; bool reply, label; uint8_t turn; };
Turn fallbackTurns[6];
ChatLine fallbackLines[256];
Turn *turns = fallbackTurns;
ChatLine *lines = fallbackLines;
int turnCapacity = 6, lineCapacity = 256;
int turnCount = 0, activeTurn = -1, buildingTurn = 0;
bool wasBusy = false, followLatest = true;
int lineCount = 0, scrollTop = 0;
constexpr int VISIBLE_LINES = 9, LINE_HEIGHT = 18;

bool inside(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && x < bx + w && y >= by && y < by + h;
}
void font(Font value) {
  g->setFont(value == Font::Small ? nullptr :
             value >= Font::Heading ? &FreeSansBold12pt7b : &FreeSans9pt7b);
  g->setTextSize(value == Font::Large ? 2 : 1);
  g->setTextWrap(false);
}
int textWidth(const char *s, Font value = Font::Body) {
  font(value);
  int16_t x, y;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x, &y, &w, &h);
  return w;
}
// Current embedded fonts contain printable ASCII. Collapse each unsupported
// UTF-8 codepoint once, with common typographic punctuation made readable.
void cleanText(const char *input, char *output, size_t capacity) {
  if (!capacity) return;
  size_t n = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(input);
  while (*p && n + 1 < capacity) {
    if (*p < 128) {
      output[n++] = (*p == '\n' || *p == '\t' || *p >= 32) ? char(*p) : ' ';
      ++p;
    } else {
      char c = '?';
      if (p[0] == 0xE2 && p[1] == 0x80 && p[2]) {
        if (p[2] == 0x98 || p[2] == 0x99) c = '\'';
        else if (p[2] == 0x9C || p[2] == 0x9D) c = '"';
        else if (p[2] >= 0x90 && p[2] <= 0x95) c = '-';
        else if (p[2] == 0xA6) c = '.';
      }
      output[n++] = c;
      ++p;
      while ((*p & 0xC0) == 0x80) ++p;
    }
  }
  output[n] = 0;
}
void text(const char *s, int x, int top, uint16_t color = TEXT,
          Font value = Font::Body, int maxWidth = 0) {
  char buffer[256];
  cleanText(s, buffer, sizeof(buffer));
  if (maxWidth && textWidth(buffer, value) > maxWidth) {
    size_t n = strlen(buffer);
    while (n && textWidth(buffer, value) + textWidth("...", value) > maxWidth)
      buffer[--n] = 0;
    strlcat(buffer, "...", sizeof(buffer));
  }
  font(value);
  int16_t bx, by;
  uint16_t w, h;
  g->getTextBounds(buffer, 0, 0, &bx, &by, &w, &h);
  g->setTextColor(color);
  g->setCursor(x - bx, top - by);
  g->print(buffer);
}
void centered(const char *s, int cx, int top, uint16_t color = TEXT, Font value = Font::Body) {
  const int maxWidth = 2 * min(cx, WIDTH - cx);
  text(s, cx - min(textWidth(s, value), maxWidth) / 2, top, color, value, maxWidth);
}
void panel(int x, int y, int w, int h, uint16_t fill = PANEL, int radius = 12) {
  g->fillRoundRect(x, y, w, h, radius, fill);
  g->drawRoundRect(x, y, w, h, radius, BORDER);
}

uint16_t shade(uint16_t color, uint8_t percent) {
  return (((color >> 11) * percent / 100) << 11) |
         ((((color >> 5) & 63) * percent / 100) << 5) |
         ((color & 31) * percent / 100);
}
uint16_t surfaceTint(uint16_t color, uint8_t percent) {
  if (!DeviceSettings::values().lightMode) return shade(color,percent);
  const unsigned rest=100-percent;
  return ((((color>>11)*percent+31*rest)/100)<<11) |
         (((((color>>5)&63)*percent+63*rest)/100)<<5) |
         (((color&31)*percent+31*rest)/100);
}
bool held(int x, int y, int w, int h) {
  return touching && !moved && inside(startX, startY, x,y,w,h) &&
         inside(fingerX, fingerY, x,y,w,h);
}
bool released(int x, int y, int w, int h) {
  return inside(startX,startY,x,y,w,h) && inside(fingerX,fingerY,x,y,w,h);
}
void button(const char *label, int x, int y, int w, int h, bool enabled = true, uint16_t accent = CYAN) {
  const bool pressed = enabled && held(x,y,w,h);
  panel(x, y, w, h, pressed ? surfaceTint(accent,75) : enabled ? RAISED : PANEL, 9);
  if (enabled) g->drawRoundRect(x, y, w, h, 9, pressed ? TEXT : accent);
  if (textWidth(label)<=w-16) centered(label,x+w/2,y+(h-13)/2,enabled ? TEXT : MUTED);
  else text(label,x+8,y+(h-13)/2,enabled ? TEXT : MUTED,Font::Small,w-16);
}

void icon(Page which, int cx, int cy, uint16_t color, uint16_t bg) {
  switch (which) {
    case Page::Home:
      g->fillTriangle(cx - 13, cy - 1, cx, cy - 13, cx + 13, cy - 1, color);
      g->fillRoundRect(cx - 10, cy - 2, 21, 15, 3, color);
      g->fillRoundRect(cx - 3, cy + 4, 7, 10, 2, bg);
      break;
    case Page::Music:
      g->fillRect(cx + 4, cy - 13, 3, 22, color);
      g->fillTriangle(cx - 8, cy - 11, cx + 7, cy - 15, cx + 7, cy - 9, color);
      g->fillRect(cx - 9, cy - 11, 3, 24, color);
      g->fillEllipse(cx - 11, cy + 13, 6, 4, color);
      g->fillEllipse(cx + 2, cy + 8, 6, 4, color);
      break;
    case Page::Chat:
      g->drawRoundRect(cx - 13, cy - 10, 27, 21, 9, color);
      g->drawRoundRect(cx - 12, cy - 9, 25, 19, 8, color);
      g->drawLine(cx - 8, cy + 7, cx - 12, cy + 15, color);
      g->drawLine(cx - 12, cy + 15, cx - 1, cy + 10, color);
      break;
    case Page::Settings:
      for (int i = 0; i < 8; ++i) {
        const float a = i * 0.78539816f;
        const int x = cosf(a) * 14, y = sinf(a) * 14;
        g->fillRoundRect(cx + x - 2, cy + y - 2, 5, 5, 1, color);
      }
      g->drawCircle(cx, cy, 11, color);
      g->drawCircle(cx, cy, 10, color);
      g->drawCircle(cx, cy, 4, color);
      break;
  }
}
void wifiIcon(int cx, int cy, uint16_t color) {
  g->drawLine(cx - 10, cy - 4, cx, cy - 8, color);
  g->drawLine(cx, cy - 8, cx + 10, cy - 4, color);
  g->drawLine(cx - 7, cy, cx, cy - 3, color);
  g->drawLine(cx, cy - 3, cx + 7, cy, color);
  g->fillCircle(cx, cy + 5, 2, color);
}
void shortcut(Page destination, int x) {
  panel(x, SHORTCUT_Y, SHORTCUT_SIZE, SHORTCUT_SIZE, RAISED, 10);
  const int cx=x+SHORTCUT_SIZE/2;
  icon(destination,cx,SHORTCUT_Y+SHORTCUT_SIZE/2,TEXT,RAISED);
}
void settingsIcon() {
  constexpr int cx=SETTINGS_X+SETTINGS_W/2, cy=16;
  g->drawCircle(cx,cy,8,TEXT);
  g->drawCircle(cx,cy,3,TEXT);
  for (int i=0;i<8;++i) {
    const float angle=i*0.78539816f;
    g->drawLine(cx+int(cosf(angle)*8),cy+int(sinf(angle)*8),
                cx+int(cosf(angle)*11),cy+int(sinf(angle)*11),TEXT);
  }
}
CompanionAssistant::Connection aiConnection() {
  return wasConnected ? assistant.connection : CompanionAssistant::Connection::Offline;
}
void aiIndicator() {
  using C=CompanionAssistant::Connection;
  aiIndicatorState = int(aiConnection());
  const uint16_t color = aiConnection() == C::Ready ? GREEN :
      aiConnection() == C::Checking || aiConnection() == C::ApiError ? AMBER : MUTED;
  g->fillRect(258,0,34,33,BACKGROUND);
  g->drawRoundRect(264,6,22,22,6,color);
  text("AI",269,14,color,Font::Small);
  if (aiConnection() == C::Offline) g->drawLine(263,28,287,5,RED);
}
void header() {
  const time_t now = time(nullptr);
  struct tm local = {};
  char clock[16] = "--:--", date[40] = "Clock awaiting Wi-Fi";
  if (Alarms::clockReady(now) && localtime_r(&now, &local)) {
    strftime(clock, sizeof(clock), "%H:%M", &local);
    strftime(date, sizeof(date), "%a, %b %d", &local);
  }
  char key[80];
  snprintf(key, sizeof(key), "%s %s %d %d %d", clock, date, wasConnected, Storage::info().available, music.online);
  if (!headerDirty && !strcmp(key, headerKey)) {
    if (aiIndicatorState != int(aiConnection())) aiIndicator();
    return;
  }
  strlcpy(headerKey, key, sizeof(headerKey));
  headerDirty = false;
  ConversationStatus::invalidate();
  g->fillRect(0, 0, WIDTH, HEADER_HEIGHT, BACKGROUND);
  const bool clockPressed = held(0, 0, ClockW, HEADER_HEIGHT);
  if (clockPressed || page >= Page::Alarms)
    panel(4, 3, ClockW-9, 27, clockPressed ? surfaceTint(CYAN, 55) : RAISED, 9);
  text(clock, 12, 7);
  g->drawFastVLine(ClockW, 8, 17, BORDER);

  text(date, 90, 12, MUTED, Font::Small, 146);
  aiIndicator();
  // A compact desktop-music link indicator, separate from Wi-Fi connectivity.
  const uint16_t link = music.online ? GREEN : MUTED;
  g->fillRect(304, 8, 2, 14, link); g->fillRect(313, 6, 2, 14, link);
  g->drawLine(305,8,314,6,link); g->drawLine(305,9,314,7,link);
  g->fillEllipse(301,22,4,3,link); g->fillEllipse(310,20,4,3,link);
  g->fillCircle(323,10,2,link);
  wifiIcon(353, 15, wasConnected ? TEXT : MUTED);
  g->drawRoundRect(390, 8, 14, 18, 2, Storage::info().available ? CYAN : MUTED);
  text("SD",391, 14, Storage::info().available ? CYAN : MUTED, Font::Small);
  settingsIcon();
  g->drawFastHLine(0, 33, contentWidth()-4, BORDER);
}
// Word wrapping also breaks oversized words. No renderer writes outside its box.
template<typename Emit>
void wrap(const char *input, int width, Emit emit, Font value = Font::Body) {
  char ascii[2048], line[64] = {};
  cleanText(input, ascii, sizeof(ascii));
  const char *p = ascii;
  while (*p) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) break;
    if (*p == '\n') { emit(""); ++p; continue; }
    size_t length = 0, lastSpace = 0;
    while (p[length] && p[length] != '\n' && length + 1 < sizeof(line)) {
      line[length] = p[length]; line[length + 1] = 0;
      if (textWidth(line, value) > width) break;
      if (p[length] == ' ') lastSpace = length;
      ++length;
    }
    if (p[length] && p[length] != '\n' && lastSpace) length = lastSpace;
    if (!length) length = 1;
    memcpy(line, p, length);
    line[length] = 0;
    size_t trim = length;
    while (trim && line[trim - 1] == ' ') line[--trim] = 0;
    emit(line);
    p += length;
    if (*p == '\n') ++p;
  }
}
void paragraph(const char *s, int x, int top, int width, int maxLines, uint16_t color = TEXT) {
  int index = 0;
  wrap(s, width, [&](const char *line) {
    if (index < maxLines) text(line, x, top + index * 20, color, Font::Body, width);
    ++index;
  });
}
int localHour() {
  const time_t now=time(nullptr);
  struct tm local{};
  return now >= 1704067200 && localtime_r(&now,&local) ? local.tm_hour : -1;
}
bool homeListening() {
  return conversation.active() && audio.route == Sound::Route::Recording;
}

bool homeCanCancel() {
  return !homeCanceling && (homeListening() || (conversation.active() && !assistant.busy && !assistant.replyReady) ||
      (assistant.homeTurn && (assistant.busy || assistant.replyReady)) ||
      replyPlaybackPending || audio.route == Sound::Route::AssistantPlayback);
}

void homeRightShortcut() {
  if (!homeCanCancel()) {
    shortcut(Page::Chat,HOME_X);
    return;
  }
  const bool light=DeviceSettings::values().lightMode;
  const uint16_t fill=light ? surfaceTint(RED,18) : 0x3968;
  const uint16_t fg=light ? 0x8125 : 0xF5FA;
  panel(HOME_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE,fill,10);
  constexpr int cx = HOME_X + SHORTCUT_SIZE / 2;
  constexpr int cy = SHORTCUT_Y + SHORTCUT_SIZE / 2;
  for (int offset = -1; offset <= 1; ++offset) {
    g->drawLine(cx - 8, cy - 8 + offset, cx + 8, cy + 8 + offset, fg);
    g->drawLine(cx - 8, cy + 8 + offset, cx + 8, cy - 8 + offset, fg);
  }
}

constexpr int HOME_TEXT_PADDING = 5;
constexpr int HOME_TEXT_LEFT = LEFT_SHORTCUT_X + SHORTCUT_SIZE + HOME_TEXT_PADDING;
constexpr int HOME_TEXT_RIGHT = HOME_X - HOME_TEXT_PADDING;
constexpr int HOME_TEXT_WIDTH = HOME_TEXT_RIGHT - HOME_TEXT_LEFT;
constexpr int HOME_TEXT_CENTER_X = (HOME_TEXT_LEFT + HOME_TEXT_RIGHT) / 2;
constexpr int HOME_TEXT_CENTER_Y = SHORTCUT_Y + SHORTCUT_SIZE / 2;

void homeTranscript(const char *value) {
  char lines[2][64] = {};
  unsigned rows = 0;
  bool more = false;
  wrap(value, HOME_TEXT_WIDTH, [&](const char *line) {
    if (rows < 2) strlcpy(lines[rows++], line, sizeof(lines[0]));
    else more = true;
  }, Font::Heading);
  if (!rows) return;
  if (more) {
    auto &last = lines[rows - 1];
    size_t length = strlen(last);
    while (length && (length + 4 > sizeof(last) ||
           textWidth(last, Font::Heading) + textWidth("...", Font::Heading) > HOME_TEXT_WIDTH))
      last[--length] = 0;
    strlcat(last, "...", sizeof(last));
  }

  constexpr int lineGap = 4;
  uint16_t height[2] = {};
  int blockHeight = rows == 2 ? lineGap : 0;
  font(Font::Heading);
  for (unsigned i = 0; i < rows; ++i) {
    int16_t x, y;
    uint16_t width;
    g->getTextBounds(lines[i], 0, 0, &x, &y, &width, &height[i]);
    blockHeight += height[i];
  }
  int top = HOME_TEXT_CENTER_Y - blockHeight / 2;
  for (unsigned i = 0; i < rows; ++i) {
    centered(lines[i], HOME_TEXT_CENTER_X, top, TEXT, Font::Heading);
    top += height[i] + lineGap;
  }
}

void homeText() {
  g->fillRect(HOME_TEXT_LEFT, 235, HOME_TEXT_WIDTH, HEIGHT - 235, BACKGROUND);
  if (!homeCanceling && audio.route == Sound::Route::AssistantPlayback) {
    const auto span = HomeCaptions::at(assistant.reply, audio.replyPlayedFrames, audio.replyTotalFrames);
    char caption[96];
    const size_t length = min(span.length, sizeof(caption) - 1);
    memcpy(caption, assistant.reply + span.offset, length);
    caption[length] = 0;
    homeTranscript(caption);
    return;
  }
  if (!homeCanceling && assistant.homeTurn && assistant.visible && assistant.transcript[0] &&
      !assistant.prompt && (assistant.busy || assistant.replyReady || replyPlaybackPending)) {
    homeTranscript(assistant.transcript);
    return;
  }
  if (homeCanCancel()) return;
  const int hour = localHour();
  const char *greeting = hour < 0 ? "Hello!" : hour < 12 && hour >= 5 ? "Good morning!" :
      hour >= 12 && hour < 17 ? "Good afternoon!" : "Good evening!";
  centered(noSpeechAt ? "No speech heard" : greeting, WIDTH / 2, 257, TEXT, Font::Heading);
  centered("How can I help you?", WIDTH / 2, 290);
}

void home() {
  Face::setLayout(contentWidth());
  Face::redraw();
  homeText();
  shortcut(Page::Music,LEFT_SHORTCUT_X);
  homeRightShortcut();
}

bool matchingArtwork() {
  return music.online && music.videoId[0] && artwork.ready &&
         !strcmp(artwork.videoId, music.videoId);
}
uint16_t musicColor() { return matchingArtwork() ? artwork.accent : AMBER; }
int playbackPhase(uint32_t now) { return music.online && music.trackState == 1 ? int((now/260)%3) : 3; }
void playingDots(uint32_t now) {
  dotPhase = playbackPhase(now);
  g->fillRoundRect(67, 195, 42, 18, 8, PANEL);
  for (int i = 0; i < 3; ++i)
    g->fillCircle(76+i*12, 204, 3, dotPhase == i ? musicColor() : BORDER);
}

constexpr int TRANSPORT_Y=SHORTCUT_Y, TRANSPORT_SIDE=SHORTCUT_SIZE;
int transportX(int index) {
  const int origins[]={122,184,326,LEFT_SHORTCUT_X};
  return origins[index];
}
int transportWidth(int index) { return index == 1 ? 132 : TRANSPORT_SIDE; }
void transportButtons(int only = -1) {
  const uint16_t accent = musicColor();
  for (int i = 0; i < 4; ++i) {
    if (only >= 0 && only != i) continue;
    const int x = transportX(i), y = TRANSPORT_Y, width = transportWidth(i), cx = x+width/2, cy = y+TRANSPORT_SIDE/2;
    const bool enabled = music.online && (i!=3 || (assistant.configured && audio.microphoneReady && !assistant.busy && audio.route==Sound::Route::Idle));
    const bool down = enabled && (held(x,y,width,TRANSPORT_SIDE) || flashTransport == i);
    const uint16_t fill = down ? surfaceTint(accent,78) : i == 1 && music.online ? surfaceTint(accent,36) : PANEL;
    panel(x,y,width,TRANSPORT_SIDE,fill,10);
    g->drawRoundRect(x,y,width,TRANSPORT_SIDE,10,down ? TEXT : enabled ? accent : BORDER);
    const uint16_t fg = enabled ? TEXT : MUTED;
    if (i == 0) {
      g->fillRect(cx-12,cy-11,3,22,fg);
      g->fillTriangle(cx+10,cy-11,cx-6,cy,cx+10,cy+11,fg);
    } else if (i == 2) {
      g->fillRect(cx+10,cy-11,3,22,fg);
      g->fillTriangle(cx-10,cy-11,cx+6,cy,cx-10,cy+11,fg);
    } else if (i == 3) {
      g->drawCircle(cx-3,cy-3,8,fg);
      g->drawLine(cx+3,cy+3,cx+12,cy+12,fg);
    } else if (music.trackState == 1) {
      g->fillRect(cx-8,cy-11,5,22,fg); g->fillRect(cx+3,cy-11,5,22,fg);
    } else g->fillTriangle(cx-7,cy-12,cx+10,cy,cx-7,cy+12,fg);
  }
}

void durationText(char *out, size_t capacity, uint32_t milliseconds) {
  const uint32_t seconds = milliseconds / 1000;
  if (seconds >= 3600)
    snprintf(out, capacity, "%lu:%02lu:%02lu", (unsigned long)(seconds/3600),
             (unsigned long)((seconds/60)%60), (unsigned long)(seconds%60));
  else
    snprintf(out, capacity, "%lu:%02lu", (unsigned long)(seconds/60), (unsigned long)(seconds%60));
}
void musicProgress(uint32_t now) {
  const int width = contentWidth()-192;
  g->fillRect(180, 192, width, 32, BACKGROUND);
  if (!music.online || !music.progressAvailable || !music.durationMs) {
    text(music.online ? (music.adPlaying ? "Advertisement" : "Time unavailable") :
         "Desktop offline", 180, 204, MUTED, Font::Small, width);
    progressSecond = UINT32_MAX;
    return;
  }
  const uint32_t position = Music::positionMsAt(music, now);
  progressSecond = position/1000;
  g->fillRoundRect(180,195,width,5,2,BORDER);
  const int filled = uint64_t(position)*width/music.durationMs;
  if (filled >= 4) g->fillRoundRect(180,195,filled,5,2,musicColor());
  else if (filled) g->fillRect(180,195,filled,5,musicColor());
  char elapsed[20], duration[20];
  durationText(elapsed,sizeof(elapsed),position);
  durationText(duration,sizeof(duration),music.durationMs);
  text(elapsed,180,210,MUTED,Font::Small);
  text(duration,contentWidth()-12-textWidth(duration,Font::Small),210,MUTED,Font::Small);
}
void musicArtworkPanel() {
  const uint16_t accent = musicColor();
  panel(12, 72, 152, 152, RAISED);
  if (artworkPixels && matchingArtwork() && copiedArtwork == artwork.revision) {
    g->draw16bitRGBBitmap(18,78,artworkPixels,MusicArtwork::SIDE,MusicArtwork::SIDE);
  } else {
    icon(Page::Music,88,139,accent,RAISED);
    centered(artwork.loading && !strcmp(artwork.videoId,music.videoId) ?
             "Loading artwork" : "No artwork",88,177,MUTED,Font::Small);
  }
  playingDots(millis());
}
void musicDetails() {
  const int width = contentWidth()-192;
  g->fillRect(180,72,width,120,BACKGROUND);
  paragraph(music.title[0] ? music.title : "No track selected",180,78,width,3);
  paragraph(music.artist,180,147,width,2,MUTED);
  musicProgress(millis());
}
void musicVoiceStatus() {
  g->fillRect(12,40,contentWidth()-24,26,BACKGROUND);
  const char *status=audio.musicCaptureFailed || audio.route==Sound::Route::Recording ? audio.message :
      assistant.music && assistant.visible ? assistant.status : "";
  if (status[0]) text(status,16,48,MUTED,Font::Small,contentWidth()-32);
}
void musicScreen() {
  musicVoiceStatus();
  musicArtworkPanel();
  musicDetails();
  transportButtons();
}

void addLine(const char *s, bool reply, bool label) {
  if (lineCount >= lineCapacity) return;
  auto &line = lines[lineCount++];
  strlcpy(line.text, s, sizeof(line.text));
  line.reply = reply; line.label = label; line.turn = buildingTurn;
}
void rebuildConversation() {
  lineCount = 0;
  for (int i = 0; i < turnCount; ++i) {
    buildingTurn = i;
    if (turns[i].question[0]) {
      addLine("YOU", false, true);
      wrap(turns[i].question, 280, [&](const char *s) { addLine(s, false, false); });
    }
    if (turns[i].answer[0]) {
      addLine("SAM", true, true);
      wrap(turns[i].answer, 280, [&](const char *s) { addLine(s, true, false); });
    }
  }
  const int maximum = max(0, lineCount - VISIBLE_LINES);
  scrollTop = followLatest ? maximum : min(scrollTop, maximum);
}
void newTurn() {
  if (turnCount == turnCapacity) {
    int removed = 0;
    while (removed < lineCount && lines[removed].turn == 0) ++removed;
    if (!followLatest) scrollTop = max(0, scrollTop-removed);
    for (int i = 1; i < turnCapacity; ++i) turns[i - 1] = turns[i];
    --turnCount;
  }
  activeTurn = turnCount++;
  turns[activeTurn] = Turn{};
}
bool captureConversation(const CompanionAssistant::Snapshot &next) {
  bool changed = false;
  if (next.music || next.prompt) { wasBusy=false; activeTurn=-1; return false; }
  if (next.busy && !wasBusy) { newTurn(); changed = true; }
  if (activeTurn < 0 && next.visible && next.transcript[0] &&
      (turnCount == 0 || strcmp(turns[turnCount - 1].question, next.transcript) ||
       strcmp(turns[turnCount - 1].answer, next.reply))) {
    newTurn();
  }
  if (activeTurn >= 0 && next.visible) {
    auto &turn = turns[activeTurn];
    if (next.transcript[0] && strcmp(turn.question, next.transcript)) {
      strlcpy(turn.question, next.transcript, sizeof(turn.question)); changed = true;
    }
    if (next.reply[0] && strcmp(turn.answer, next.reply)) {
      strlcpy(turn.answer, next.reply, sizeof(turn.answer)); changed = true;
    }
  }
  if (!next.busy) activeTurn = -1;
  wasBusy = next.busy;
  if (changed) rebuildConversation();
  return changed;
}

int chatScrollUpX() { return contentWidth()-58; }
int chatScrollDownX() { return contentWidth()-58; }
int chatJumpX() { return contentWidth()-150; }
void chatHeader() {
  // Compact avatar, reserving most of the screen for conversation.
  g->fillRoundRect(12, 43, 26, 24, 9, CYAN);
  g->fillRoundRect(15, 46, 20, 18, 7, BACKGROUND);
  g->fillCircle(21, 53, 2, TEXT); g->fillCircle(29, 53, 2, TEXT);
  g->drawFastHLine(22, 59, 7, CYAN);
  text("Conversation", 48, 44, TEXT, Font::Heading);
  if (!followLatest) button("Jump to latest", chatJumpX(), 43, 138, 26, true, PURPLE);

}

void chatConversation() {
  panel(12, 79, contentWidth()-24, 169);
  if (!lineCount) {
    centered("Ready when you are", contentWidth()/2, 116);
    centered("Say your wake phrase, or tap Talk.", contentWidth()/2, 149, MUTED, Font::Small);
    centered(assistant.configured ? "Replies will appear here." : "Cloud setup is still pending.",
             contentWidth()/2, 174, MUTED, Font::Small);
  } else {
    int row = 0;
    while (row < VISIBLE_LINES && scrollTop + row < lineCount) {
      const int first = row;
      const bool reply = lines[scrollTop+row].reply;
      ++row;
      while (row < VISIBLE_LINES && scrollTop+row < lineCount &&
             !lines[scrollTop+row].label && lines[scrollTop+row].reply == reply) ++row;
      const int top = 82 + first*LINE_HEIGHT;
      const int height = (row-first)*LINE_HEIGHT;
      int width = 110;
      for (int i=first; i<row; ++i)
        width = max(width,textWidth(lines[scrollTop+i].text,lines[scrollTop+i].label ? Font::Small : Font::Body)+20);
      width = min(width,contentWidth()-94);
      const int left = reply ? contentWidth()-70-width : 18;
      g->fillRoundRect(left,top,width,height,7,surfaceTint(reply ? PURPLE : CYAN,16));
      for (int i=first;i<row;++i) {
        const auto &line=lines[scrollTop+i];
        const int y=83+i*LINE_HEIGHT;
        text(line.text, left+10, y+(line.label ? 3 : 0),
             line.label ? MUTED : TEXT, line.label ? Font::Small : Font::Body,
             width-20);
      }
    }
  }
}
void chatTimer() {
  g->fillRect(12,251,contentWidth()-24,SHORTCUT_Y-251,BACKGROUND);
  const bool recording = audio.route == Sound::Route::Recording;
  char detail[80];
  if (recording) snprintf(detail, sizeof(detail), "Recording %lu.%lus",
      (unsigned long)(audio.recordedMs / 1000), (unsigned long)(audio.recordedMs % 1000 / 100));
  else strlcpy(detail, audio.message, sizeof(detail));
  text(detail, 12, 251, recording ? RED : MUTED, Font::Small, contentWidth()-24);
}
void chatScrollButtons() {
  button("^", chatScrollUpX(), 84, 42, 44, scrollTop > 0, PURPLE);
  button("v", chatScrollDownX(), 202, 42, 44, scrollTop < max(0, lineCount - VISIBLE_LINES), PURPLE);
}
void chatControls(int only = -1) {
  const bool recording = audio.route == Sound::Route::Recording;
  const bool idle = !assistant.busy && !assistant.replyReady && !Sound::assistantReserved();
  if (only < 0 || only == 0) button(recording ? "Finish" : "Talk", CHAT_TALK_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE,
         idle && audio.microphoneReady && assistant.configured && !audio.teaching, recording ? RED : CYAN);
  if (only < 0 || only == 1) button(audio.teaching ? "Say your phrase..." : audio.wakeTrained ? "Re-teach phrase" : "Teach phrase",
         CHAT_TEACH_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE,
         idle && audio.microphoneReady && audio.route==Sound::Route::Idle && !audio.teaching,PURPLE);
}

void chatStatus() {
  g->fillRect(48,70,contentWidth()-60,8,BACKGROUND);
  text(!assistant.configured ? "AI setup pending" : assistant.music ? "Ready for a question" : assistant.status,
       48,70,assistant.busy ? PURPLE : MUTED,Font::Small,contentWidth()-70);
}
void chatJump() {
  g->fillRect(chatJumpX(),43,138,26,BACKGROUND);
  if (!followLatest) button("Jump to latest",chatJumpX(),43,138,26,true,PURPLE);
}
void chatScreen() {
  chatHeader(); chatStatus(); chatConversation(); chatTimer(); chatScrollButtons(); chatControls();
}
void markChatControls(int x,int y) {
  if (inside(x,y,CHAT_TALK_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE)) chatControlsDirty |= 1;
  if (inside(x,y,CHAT_TEACH_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE)) chatControlsDirty |= 2;
  if (inside(x,y,chatScrollUpX(),84,42,44) || inside(x,y,chatScrollDownX(),202,42,44) || inside(x,y,chatJumpX(),43,138,26))
    chatScrollDirty = true;
}

void slider(int value, int low, int high, uint16_t accent = CYAN) {
  const int x = 158 + settingsShift(), y = 172, w = 208;
  const int fill = (value - low) * w / (high - low);
  g->fillRoundRect(x, y - 3, w, 7, 3, BORDER);
  if (fill) g->fillRoundRect(x, y - 3, fill, 7, 3, accent);
  g->fillCircle(x + fill, y, 8, TEXT);
  text("MIN", x, y + 20, MUTED, Font::Small);
  text("MAX", x + w - 18, y + 20, MUTED, Font::Small);
}
void settingsScreen() {
  text("Settings", 12, 47, TEXT, Font::Heading);
  const char *categories[] = {"Display", "Wi-Fi", "Sound", "Microphone", "Lighting", "Storage", "Device"};
  for (int i = 0; i < 7; ++i) {
    const int y = 77 + i * 33;
    if (category == i) g->fillRoundRect(12, y - 6, 116, 31, 7, RAISED);
    text(categories[i], 20, y + 2, category == i ? CYAN : MUTED, Font::Body, 100);
  }
  panel(140,72,HOME_X-148,238);
  const int shift = settingsShift();
  const auto &prefs = DeviceSettings::values();
  char buffer[100];
  switch (category) {
    case 0:
      text("Brightness", 154+shift, 88);
      snprintf(buffer, sizeof(buffer), "%u%%", prefs.brightness);
      text(buffer, 154+shift, 117, TEXT, Font::Heading);
      slider(prefs.brightness, 10, 100);
      text("Drag the slider to adjust.", 154+shift, 219, MUTED, Font::Small);
      text("Clock: India / UTC+05:30", 154+shift, 252, MUTED, Font::Small);
      text("Time syncs over Wi-Fi.", 154+shift, 269, MUTED, Font::Small);
      break;
    case 1:
      text(wasConnected ? "Connected" : "Offline", 154+shift, 88, wasConnected ? GREEN : MUTED);
      text("NETWORK", 154+shift, 122, MUTED, Font::Small);
      text(wasConnected ? ssid.c_str() : "Not connected", 154+shift, 139, TEXT, Font::Body, 220);
      text("IP ADDRESS", 154+shift, 174, MUTED, Font::Small);
      text(wasConnected ? address.c_str() : "--", 154+shift, 191, TEXT, Font::Body, 220);
      snprintf(buffer, sizeof(buffer), "Signal: %d dBm", signalDb);
      text(wasConnected ? buffer : "Retrying saved network", 154+shift, 226, MUTED, Font::Small);
      text("Edit /companion.json on SD.", 154+shift, 270, MUTED, Font::Small);
      break;
    case 2:
      text("Speaker level", 154+shift, 88);
      snprintf(buffer, sizeof(buffer), "%u%%", prefs.volume);
      text(buffer, 154+shift, 117, TEXT, Font::Heading);
      slider(prefs.volume, 0, 100);
      text(audioReady ? "ES8311 output volume" : "Speaker unavailable", 154+shift, 219, MUTED, Font::Small);
      text("Saved after adjustment.", 154+shift, 252, MUTED, Font::Small);
      break;
    case 3:
      text("Microphone level", 154+shift, 88);
      snprintf(buffer, sizeof(buffer), "%+d dB", prefs.microphoneDb);
      text(buffer, 154+shift, 117, TEXT, Font::Heading);
      slider(prefs.microphoneDb, -12, 12, PURPLE);
      text(audio.microphoneReady ? "ES7210 digital gain" : "Microphone unavailable", 154+shift, 219, MUTED, Font::Small);
      snprintf(buffer, sizeof(buffer), "Recording: %u seconds", prefs.recordSeconds);
      text(buffer, 154+shift, 245, TEXT, Font::Small);
      button("-",154+shift,265,65,30,true,PURPLE);
      button("+",309+shift,265,65,30,true,PURPLE);
      text("10 to 20 sec",226+shift,277,MUTED,Font::Small);
      break;
    case 4: {
      text("LED color", 154+shift, 88);
      button("Per screen", 154+shift, 118, 220, 31, true, prefs.palette == 0 ? CYAN : BORDER);
      const uint16_t colors[] = {CYAN, BLUE, PURPLE, AMBER, GREEN};
      for (int i = 0; i < 5; ++i) {
        const int x = 173 + shift + i * 44;
        g->fillCircle(x, 178, 13, colors[i]);
        if (prefs.palette == i + 1) g->drawCircle(x, 178, 17, TEXT);
      }
      button("Off", 154+shift, 211, 220, 31, true, prefs.palette == 6 ? CYAN : BORDER);
      text("Home cyan / Music artwork", 154+shift, 261, MUTED, Font::Small);
      text("Chat violet / Settings blue", 154+shift, 280, MUTED, Font::Small);
      break;
    }
    case 5: {
      const auto storage = Storage::info();
      text(storage.available ? "microSD" : "No card mounted", 154+shift, 88);
      if (storage.available) {
        constexpr double GB = 1024.0 * 1024 * 1024;
        snprintf(buffer, sizeof(buffer), "Total  %.2f GB", storage.totalBytes / GB);
        text(buffer, 154+shift, 123, TEXT, Font::Body, 220);
        snprintf(buffer, sizeof(buffer), "Used  %.2f GB", storage.usedBytes / GB);
        text(buffer, 154+shift, 155, TEXT, Font::Body, 220);
        const uint64_t free = storage.totalBytes > storage.usedBytes ? storage.totalBytes - storage.usedBytes : 0;
        snprintf(buffer, sizeof(buffer), "Free   %.2f GB", free / GB);
        text(buffer, 154+shift, 187, TEXT, Font::Body, 220);
        text("Cached; refresh while idle.", 154+shift, 224, MUTED, Font::Small);
      } else paragraph("SD card did not mount. Check startup Serial output.", 154+shift, 128, 218, 3, MUTED);
      button("Refresh", 154+shift, 260, 220, 33, storage.available);
      break;
    }
    case 6:
      text("Desktop Companion", 154+shift, 88, TEXT, Font::Body, 220);
      text(FIRMWARE, 154+shift, 118, CYAN, Font::Small, 220);
      text(ESP.getChipModel(), 154+shift, 147, TEXT, Font::Body, 220);
      snprintf(buffer, sizeof(buffer), "Flash %u MB / PSRAM %u MB",
        unsigned(ESP.getFlashChipSize() / 1048576), unsigned(ESP.getPsramSize() / 1048576));
      text(buffer, 154+shift, 180, MUTED, Font::Small, 220);
      text("480 x 320 / landscape", 154+shift, 204, MUTED, Font::Small);
      text("Build " __DATE__, 154+shift, 228, MUTED, Font::Small);
      text("ESP32 core " ESP_ARDUINO_VERSION_STR, 154+shift, 252, MUTED, Font::Small);
      button("Power off",154+shift,270,220,30);
      break;
  }
}
void showNotice(const char *value) {
  strlcpy(notice, value, sizeof(notice));
  noticeAt = millis();
  dirty = true;
  Serial.printf("UI: %s\n", value);
}
void formatAlarmTime(time_t when, char (&label)[16]) {
  struct tm local{};
  label[0] = 0;
  if (!localtime_r(&when, &local)) return;
  strftime(label, sizeof(label), "%I:%M %p", &local);
  if (label[0] == '0') memmove(label, label + 1, strlen(label));
}

void formatAlarmDay(time_t when, time_t now, char (&label)[24]) {
  struct tm date{}, today{};
  if (!localtime_r(&when, &date) || !localtime_r(&now, &today)) {
    strlcpy(label, "Once", sizeof(label));
    return;
  }
  if (date.tm_yday == today.tm_yday && date.tm_year == today.tm_year) {
    strlcpy(label, "Today", sizeof(label));
    return;
  }
  ++today.tm_mday;
  if (mktime(&today) != -1 && date.tm_yday == today.tm_yday && date.tm_year == today.tm_year)
    strlcpy(label, "Tomorrow", sizeof(label));
  else
    strftime(label, sizeof(label), "%a, %b %d", &date);
}

int alarmSlots(int (&slots)[Alarms::Capacity]) {
  int count = 0;
  for (int i = 0; i < Alarms::Capacity; ++i) {
    if (!alarmState.entries[i].when) continue;
    int pos = count++;
    while (pos > 0 && alarmState.entries[slots[pos-1]].when > alarmState.entries[i].when) {
      slots[pos] = slots[pos-1];
      --pos;
    }
    slots[pos] = i;
  }
  return count;
}

void alarmEditorRegions(uint8_t regions) {
  for (const auto &step : AlarmSteps)
    if (regions & step.buttonRegion) button(step.delta > 0 ? "+" : "-", step.x, step.y, 56, 29);
  if (regions & HourValue) {
    char value[8];
    snprintf(value, sizeof(value), "%02d", alarmHour);
    g->fillRect(137, 144, 64, 26, PANEL);
    centered(value, 169, 146, TEXT, Font::Heading);
  }
  if (regions & MinuteValue) {
    char value[8];
    snprintf(value, sizeof(value), "%02d", alarmMinute);
    g->fillRect(279, 144, 64, 26, PANEL);
    centered(value, 311, 146, TEXT, Font::Heading);
  }
  if (regions & AlarmDay) {
    button("Next occurrence", 77, 215, 153, 35, true, alarmTomorrow ? BORDER : CYAN);
    button("Tomorrow", 250, 215, 153, 35, true, alarmTomorrow ? CYAN : BORDER);
  }
  if (regions & AlarmPreview) {
    g->fillRect(125, 272, 140, 31, BACKGROUND);
    const time_t now = time(nullptr);
    const time_t when = Alarms::nextTime(now, alarmHour, alarmMinute, alarmTomorrow);
    char preview[48] = "Waiting for time sync";
    if (notice[0]) strlcpy(preview, notice, sizeof(preview));
    else if (when) {
      char day[24], time[16];
      formatAlarmDay(when, now, day);
      formatAlarmTime(when, time);
      snprintf(preview, sizeof(preview), "%s at %s", day, time);
    }
    const int width = min(136, textWidth(preview, Font::Small));
    text(preview, 195 - width/2, 280, notice[0] ? TEXT : MUTED, Font::Small, 136);
  }
}

uint8_t alarmStepRegion(int x, int y) {
  for (const auto &step : AlarmSteps)
    if (inside(x,y,step.x,step.y,56,29)) return step.buttonRegion;
  return 0;
}

void alarmScreen() {
  const time_t now = time(nullptr);
  button(editingAlarm ? "Cancel" : "< Back", 12, 40, 82, 36);
  text(editingAlarm ? (alarmSlot == Alarms::NoSlot ? "New alarm" : "Edit alarm") : "Alarms",
       110, 47, TEXT, Font::Heading);
  if (editingAlarm) {
    panel(104, 85, 272, 124, PANEL, 18);
    centered("Hour (24h)", 169, 96, MUTED, Font::Small);
    centered("Minute", 311, 96, MUTED, Font::Small);
    centered(":", 240, 145, MUTED, Font::Heading);
    if (alarmSlot != Alarms::NoSlot) button("Delete", 12, AlarmActionY, 112, 44, true, RED);
    button("Save alarm", 266, AlarmActionY, 140, 44, alarmClockReady && audioReady);
    alarmEditorRegions(0xff);
    return;
  }

  int slots[Alarms::Capacity];
  const int count = alarmSlots(slots);
  const int lastPage = count ? (count - 1) / AlarmRows : 0;
  alarmListPage = min(alarmListPage, lastPage);
  button("+ Add alarm", 336, 40, 132, 36, alarmClockReady && audioReady && count < Alarms::Capacity);
  if (!count) {
    panel(12, 96, 456, 136, PANEL, 18);
    centered("No alarms", 240, 138, TEXT, Font::Heading);
    centered(!audioReady ? "Speaker unavailable" : alarmClockReady ? "Tap Add alarm to set a time" :
             "Waiting for time sync", 240, 175, MUTED, Font::Small);
  }
  for (int row = 0; row < AlarmRows && alarmListPage*AlarmRows+row < count; ++row) {
    const auto &entry = alarmState.entries[slots[alarmListPage*AlarmRows+row]];
    const int y = AlarmListY + row*AlarmRowStep;
    panel(12, y, 456, AlarmRowHeight, PANEL, 14);
    char time[16], day[24];
    formatAlarmTime(entry.when, time);
    formatAlarmDay(entry.when, now, day);
    text(time, 26, y+11, entry.enabled ? TEXT : MUTED, Font::Heading);
    text(entry.enabled ? day : "Off", 213, y+15, MUTED, Font::Small);
    g->fillRoundRect(399, y+8, 52, 25, 12, entry.enabled ? CYAN : RAISED);
    g->drawRoundRect(399, y+8, 52, 25, 12, entry.enabled ? CYAN : BORDER);
    g->fillCircle(entry.enabled ? 438 : 412, y+20, 9, entry.enabled ? PANEL : MUTED);
  }
  if (lastPage) {
    button("<", 12, 278, 52, 34, alarmListPage > 0);
    button(">", 72, 278, 52, 34, alarmListPage < lastPage);
  }
  const int summaryCenter = lastPage ? 262 : 211;
  const time_t next = Alarms::next(alarmState, now);
  if (notice[0]) centered(notice, summaryCenter, 292, TEXT, Font::Small);
  else if (next) {
    char day[24], time[16], summary[56];
    formatAlarmDay(next, now, day);
    formatAlarmTime(next, time);
    snprintf(summary, sizeof(summary), "Next: %s at %s", day, time);
    centered(summary, summaryCenter, 292, MUTED, Font::Small);
  } else if (count) {
    centered(alarmClockReady ? "No active alarms" : "Waiting for time sync", summaryCenter, 292, MUTED, Font::Small);
  }
}

void ringingScreen() {
  panel(24, 48, 432, 202, PANEL, 24);
  centered("Alarm", 240, 93, TEXT, Font::Heading);
  char time[16];
  formatAlarmTime(alarmState.ringing, time);
  centered(time, 240, 140, TEXT, Font::Large);
  if (notice[0]) centered(notice, 240, 223, MUTED, Font::Small);
  else if (!audioReady) centered("Speaker unavailable", 240, 223, MUTED, Font::Small);
  button("Snooze 5 min", 24, AlarmActionY, 210, 44, true, BORDER);
  button("Stop alarm", 246, AlarmActionY, 210, 44);
}

void drawPage() {
  g->fillRect(0, HEADER_HEIGHT, WIDTH, HEIGHT - HEADER_HEIGHT, BACKGROUND);
  switch (page) {
    case Page::Home: home(); break;
    case Page::Music: musicScreen(); break;
    case Page::Chat: chatScreen(); break;
    case Page::Settings: settingsScreen(); break;
    case Page::Alarms: alarmScreen(); shortcut(Page::Home,HOME_X); return;
    case Page::Ringing: ringingScreen(); return;
  }
  if (notice[0]) {
    const int noticeWidth=page==Page::Home ? 352 : HOME_X;
    const int noticeX=page==Page::Home ? 64 : 12;
    const int y = page == Page::Music ? 35 : 282;
    panel(noticeX, y, noticeWidth-24, 28, RAISED, 8);
    text(notice, noticeX+10, y+10, TEXT, Font::Small, noticeWidth-44);
  }
  if (page!=Page::Home) shortcut(Page::Home,HOME_X);
}
void routeSpeech(bool busy) {
  using P=Sound::VoicePage;
  const P pages[]={P::Home,P::Music,P::Chat,P::Off,P::Off,P::Off};
  Sound::setVoicePage(busy ? P::Off : pages[int(page)],
      assistant.configured && CompanionNetwork::connected() && (page!=Page::Music || music.online));
}
void select(Page value) {
  if (page == value || (page == Page::Ringing && alarmState.ringing)) return;
  const bool alarmHeaderChanged = page >= Page::Alarms || value >= Page::Alarms;
  page = value;
  if ((page == Page::Settings || page >= Page::Alarms) && conversation.active()) {
    conversation.stop();
    Sound::setFollowUpListening(false);
    CompanionAssistant::setConversation(false);
  }
  lastActivity=millis();
  if (page!=Page::Home) noSpeechAt=0;
  Face::setSleeping(false);
  Face::setVoiceActivity(false,false);
  routeSpeech(assistant.busy);
  Face::setVisible(page == Page::Home);
  dirty = true;
  notice[0] = 0; flashTransport = -1; dotPhase = -1;
  if (alarmHeaderChanged) headerDirty = true;
  const char *names[] = {"Home", "Music", "Chat", "Settings", "Alarms", "Alarm ringing"};
  Serial.printf("UI: %s\n", names[int(page)]);
}
bool sliderHit(int x, int y) {
  return page == Page::Settings && (category == 0 || category == 2 || category == 3) &&
         inside(x, y, 150+settingsShift(), 150, 224, 44);
}
void adjustSlider(int x) {
  const int offset = constrain(x - 158 - settingsShift(), 0, 208);
  bool ok = false;
  if (category == 0) ok = DeviceSettings::setBrightness(10 + (offset * 90 + 104) / 208);
  if (category == 2) ok = DeviceSettings::setVolume((offset * 100 + 104) / 208);
  if (category == 3) ok = DeviceSettings::setMicrophoneDb(-12 + (offset * 24 + 104) / 208);
  if (!ok) {
    if (!notice[0]) {
      if (category==0) showNotice("Backlight adjustment unavailable");
      else if (!Storage::info().available) showNotice("SD not mounted; audio is disabled");
      else if (category==3 && !audio.microphoneReady) showNotice("Microphone did not initialize");
      else if (!audioReady) showNotice("Speaker did not initialize");
      else showNotice("Finish audio before changing its level");
    }
  } else sliderRegionDirty = true;
}

void startShutdown(uint32_t now);
void cancelHomeListening() {
  canceledWake=audio.wakeCount;
  Sound::cancelHomeWake(canceledWake);
  Sound::cancelVoice();
  CompanionAssistant::cancelHomeTurn();
  homeCanceling = true;
  conversation.stop();
  Sound::setFollowUpListening(false);
  CompanionAssistant::setConversation(false);
  homeTextDirty=true;
}
void alarmResult(Alarms::Result result) {
  using R = Alarms::Result;
  switch (result) {
    case R::Saved: dirty = true; break;
    case R::ClockRequired: showNotice("Waiting for time sync"); break;
    case R::Full: showNotice("Delete an alarm to make room"); break;
    case R::Missing: showNotice("Alarm no longer available"); break;
    case R::StorageError: showNotice("Could not save alarm"); break;
  }
}

void editAlarm(int slot) {
  alarmSlot = slot;
  alarmHour = 7;
  alarmMinute = 0;
  alarmTomorrow = false;
  if (slot == Alarms::NoSlot) {
    const time_t suggested = time(nullptr) + 300;
    struct tm local{};
    if (localtime_r(&suggested, &local)) {
      alarmHour = local.tm_hour;
      alarmMinute = local.tm_min;
    }
  }
  if (slot != Alarms::NoSlot) {
    const time_t when = alarmState.entries[slot].when;
    const time_t now = time(nullptr);
    struct tm local{};
    if (localtime_r(&when, &local)) {
      alarmHour = local.tm_hour;
      alarmMinute = local.tm_min;
      alarmTomorrow = when > now && Alarms::nextTime(now, alarmHour, alarmMinute) < when &&
                      Alarms::nextTime(now, alarmHour, alarmMinute, true) <= when;
    }
  }
  editingAlarm = true;
  notice[0] = 0;
  dirty = true;
}

void tapAlarmPage() {
  if (released(HOME_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE)) { select(Page::Home); return; }
  if (released(12,40,82,36)) {
    if (editingAlarm) { editingAlarm = false; notice[0] = 0; dirty = true; }
    else select(beforeAlarms);
    return;
  }
  if (editingAlarm) {
    for (const auto &step : AlarmSteps) {
      if (!released(step.x,step.y,56,29)) continue;
      int &value = step.valueRegion == HourValue ? alarmHour : alarmMinute;
      const int limit = step.valueRegion == HourValue ? 24 : 60;
      value = (value + limit + step.delta) % limit;
      notice[0] = 0;
      alarmEditorDirty |= step.valueRegion | AlarmPreview;
      return;
    }
    if (released(77,215,153,35) || released(250,215,153,35)) {
      alarmTomorrow = released(250,215,153,35);
      notice[0] = 0;
      alarmEditorDirty |= AlarmDay | AlarmPreview;
      return;
    }
    if (released(266,AlarmActionY,140,44) && alarmClockReady && audioReady) {
      const auto result = Alarms::save(alarmSlot, Alarms::nextTime(time(nullptr), alarmHour, alarmMinute, alarmTomorrow));
      if (result == Alarms::Result::Saved) { editingAlarm = false; notice[0] = 0; }
      alarmResult(result);
    } else if (released(12,AlarmActionY,112,44) && alarmSlot != Alarms::NoSlot) {
      const auto result = Alarms::remove(alarmSlot);
      if (result == Alarms::Result::Saved) { editingAlarm = false; notice[0] = 0; }
      alarmResult(result);
    }
    return;
  }
  int slots[Alarms::Capacity];
  const int count = alarmSlots(slots);
  if (released(336,40,132,36) && alarmClockReady && audioReady && count < Alarms::Capacity) {
    editAlarm(Alarms::NoSlot);
    return;
  }
  if (released(12,278,52,34) && alarmListPage > 0) { --alarmListPage; dirty = true; return; }
  if (released(72,278,52,34) && (alarmListPage+1)*AlarmRows < count) { ++alarmListPage; dirty = true; return; }
  for (int row = 0; row < AlarmRows && alarmListPage*AlarmRows+row < count; ++row) {
    const int y = AlarmListY + row*AlarmRowStep;
    const int slot = slots[alarmListPage*AlarmRows+row];
    if (released(380,y,88,AlarmRowHeight)) { alarmResult(Alarms::toggle(slot)); return; }
    if (released(12,y,368,AlarmRowHeight)) { editAlarm(slot); return; }
  }
}

void refreshAlarms(uint32_t uptime) {
  if (uint32_t(uptime - lastAlarmPoll) < 100) return;
  lastAlarmPoll = uptime;
  const time_t now = time(nullptr);
  if (!alarmSchedulerRunning) Alarms::update(now, uptime);
  const auto next = Alarms::snapshot();
  const bool ready = Alarms::clockReady(now);
  if (next.revision != alarmState.revision || ready != alarmClockReady) {
    if (page >= Page::Alarms) { dirty = true; if (touching) moved = true; }
  }
  const bool wasRinging = alarmState.ringing != 0;
  alarmState = next;
  alarmClockReady = ready;
  const bool ringing = alarmState.ringing != 0;
  if (ringing == wasRinging) return;
  if (audioReady) Sound::setAlarmRinging(ringing);
  if (ringing) {
    beforeRinging = page;
    if (touching) moved = true;
    select(Page::Ringing);
  } else {
    select(beforeRinging);
  }
}

void tap(int x, int y) {
  if (page == Page::Ringing) {
    if (released(24,AlarmActionY,210,44)) alarmResult(Alarms::snooze());
    else if (released(246,AlarmActionY,210,44)) Alarms::stop();
    return;
  }
  if (released(0,0,ClockW,HEADER_HEIGHT)) {
    if (page != Page::Alarms) {
      beforeAlarms = page;
      editingAlarm = false;
      alarmListPage = 0;
      select(Page::Alarms);
    }
    return;
  }
  if (released(SETTINGS_X,0,SETTINGS_W,HEADER_HEIGHT)) { select(Page::Settings); return; }
  if (released(258,0,34,34)) { showNotice(wasConnected ? assistant.connectionMessage : "Connect Wi-Fi first"); return; }
  if (released(294,0,37,34)) { showNotice(music.status); return; }
  if (page == Page::Alarms) { tapAlarmPage(); return; }
  if (released(HOME_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE)) {
    if (page!=Page::Home) select(Page::Home);
    else if (homeCanCancel()) cancelHomeListening();
    else select(Page::Chat);
    return;
  }
  if (page==Page::Home && released(LEFT_SHORTCUT_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE)) {
    select(Page::Music);
    return;
  }
  if (page == Page::Music) {
    for (int i = 0; i < 4; ++i) {
      if (released(transportX(i),TRANSPORT_Y,transportWidth(i),TRANSPORT_SIDE)) {
        if (i==3) {
          if (!assistant.configured || !music.online || assistant.busy || !Sound::startVoice(Sound::VoicePage::Music))
            showNotice("Music voice unavailable");
          else { flashTransport=3; flashAt=millis(); musicButtonsDirty|=8; }
          return;
        }
        const Music::Command commands[] = {Music::Command::Previous, Music::Command::PlayPause, Music::Command::Next};
        if (!Music::command(commands[i])) showNotice("Music busy or desktop unavailable");
        else { flashTransport = i; flashAt = millis(); musicButtonsDirty |= uint8_t(1U << i); }
        return;
      }
    }
  } else if (page == Page::Chat) {
    if (!followLatest && released(chatJumpX(),43,138,26)) { followLatest = true; scrollTop = max(0, lineCount-VISIBLE_LINES); chatRegionDirty = true; return; }
    if (released(chatScrollUpX(),84,42,44)) { const int next = max(0, scrollTop-VISIBLE_LINES); if (next < scrollTop) followLatest = false; scrollTop = next; chatRegionDirty = true; return; }
    if (released(chatScrollDownX(),202,42,44)) { scrollTop = min(max(0, lineCount-VISIBLE_LINES), scrollTop+VISIBLE_LINES); chatRegionDirty = true; return; }
    if (released(CHAT_TALK_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE)) {
      if (!assistant.configured || assistant.busy || Sound::assistantReserved() ||
          !(audio.route==Sound::Route::Recording ? Sound::stopVoice() : Sound::startVoice(Sound::VoicePage::Chat)))
        showNotice("Voice unavailable or busy");
      chatStateDirty=true;
    } else if (released(CHAT_TEACH_X,SHORTCUT_Y,CHAT_CONTROL_W,SHORTCUT_SIZE)) {
      if (assistant.busy || !Sound::teachWake()) showNotice("Wait for idle audio to teach Sam");
      chatStateDirty=true;
    }
  } else if (page == Page::Settings) {
    if (category==6 && released(154+settingsShift(),270,220,30)) { startShutdown(millis()); return; }
    if (inside(x, y, 12, 71, 116, 231)) {
      category = min(6, (y - 71) / 33);
      notice[0] = 0; dirty = true; return;
    }
    if (category == 3 && (released(154+settingsShift(),265,65,30) || released(309+settingsShift(),265,65,30))) {
      const int delta = released(154+settingsShift(),265,65,30) ? -1 : 1;
      DeviceSettings::setRecordSeconds(constrain(int(DeviceSettings::values().recordSeconds)+delta,int(SoundConfig::MIN_SECONDS),int(SoundConfig::MAX_SECONDS)));
      dirty = true; return;
    }
    if (category == 4) {
      if (released(154+settingsShift(),118,220,31)) { DeviceSettings::setPalette(0); dirty = true; }
      else if (released(154+settingsShift(),211,220,31)) { DeviceSettings::setPalette(6); dirty = true; }
      else if (inside(x, y, 151+settingsShift(), 157, 224, 43)) {
        DeviceSettings::setPalette(1 + min(4, max(0, (x - 151 - settingsShift()) / 44)));
        dirty = true;
      }
    } else if (category == 5 && released(154+settingsShift(),260,220,33) && Storage::info().available) {
      // Cache updates never race audio/assistant SD access.
      if (assistant.busy || (audioReady && !Sound::reserveForAssistant(200))) {
        showNotice("Stop audio before refreshing storage"); return;
      }
      Storage::refreshUsage();
      if (audioReady) Sound::releaseAssistant();
      dirty = true;
    }
  }
}
void startShutdown(uint32_t now) {
  const auto current=Sound::snapshot();
  if (Alarms::hasArmedAlarm()) { showNotice("Turn off active alarms before power off"); return; }
  if (assistant.busy || assistant.replyReady || current.route!=Sound::Route::Idle || current.teaching) {
    showNotice("Finish audio before power off");
    return;
  }
  shutdownAt=now;
  conversation.stop();
  CompanionAssistant::setConversation(false);
  Sound::setFollowUpListening(false);
  Sound::setVoicePage(Sound::VoicePage::Off,false);
  page=Page::Home; Face::setSleeping(false); Face::setVisible(true);
  home();
  g->fillRect(0,230,contentWidth(),90,BACKGROUND);
  centered("Goodbye!",contentWidth()/2,236,TEXT,Font::Heading);
  centered("I'm going to sleep...",contentWidth()/2,274,MUTED);
}
void remoteUpdate(uint32_t now) {
  RemoteSettings::Command command;
  if (CompanionAssistant::takeCommand(command)) {
    lastActivity=now;
    bool ok=false;
    if (!assistant.busy && !assistant.replyReady) {
      if (command.action==RemoteSettings::Command::Settings) {
        ok=DeviceSettings::apply(command.values);
        if (ok) { headerDirty=dirty=true; g->fillScreen(BACKGROUND); }
      } else if (page != Page::Ringing && (command.action==RemoteSettings::Command::Enroll || command.action==RemoteSettings::Command::Test)) {
        select(Page::Chat);
        ok=command.action==RemoteSettings::Command::Enroll ? Sound::teachWake() : Sound::testWake();
      } else if (command.action==RemoteSettings::Command::Sleep) {
        startShutdown(now); ok=shutdownAt!=0;
      }
    }
    remoteStatus.ack=command.id;
    const char *failure=assistant.busy || assistant.replyReady ? "Wait for the current AI reply" :
        !Storage::info().available ? "SD did not mount; audio/wake settings are unavailable" :
        !audioReady ? "Audio did not initialize; check startup Serial output" :
        !Sound::snapshot().microphoneReady ? "Microphone unavailable; check startup Serial output" :
        "Finish audio first; wake test also requires an enrolled phrase";
    const char *result=!ok ? failure :
        command.action==RemoteSettings::Command::Settings ? "Settings applied" :
        command.action==RemoteSettings::Command::Enroll ? "Say your wake phrase once, then pause" :
        command.action==RemoteSettings::Command::Test ? "Wake test listening for 10 seconds" : "Pod is going to sleep";
    strlcpy(remoteStatus.result,result,sizeof(remoteStatus.result));
  }
  remoteStatus.values=DeviceSettings::values();
  const auto current=Sound::snapshot();
  remoteStatus.wakeTrained=current.wakeTrained; remoteStatus.teaching=current.teaching;
  const char *test=current.wakeTest==1 ? "Listening for your wake phrase..." :
      current.wakeTest==2 ? "Wake word detected" : current.wakeTest==3 ? "No match. Try again or re-enroll." : "";
  strlcpy(remoteStatus.wakeTest,test,sizeof(remoteStatus.wakeTest));
  remoteStatus.musicOnline=music.online;
  const auto storage=Storage::info();
  remoteStatus.sdAvailable=storage.available; remoteStatus.sdTotal=storage.totalBytes; remoteStatus.sdUsed=storage.usedBytes;
  remoteStatus.flashMb=ESP.getFlashChipSize()/1048576; remoteStatus.psramMb=ESP.getPsramSize()/1048576;
  CompanionAssistant::setDeviceStatus(remoteStatus);
}

}
namespace AppUI {
void showStartup(const char *stage) {
  g=Display::graphics();
  g->fillScreen(BACKGROUND);
  Face::setVisible(true); Face::setLayout(WIDTH); Face::redraw();
  centered("Hello, I'm Sam",WIDTH/2,236,TEXT,Font::Heading);
  centered(stage,WIDTH/2,275,MUTED);
}
void begin(bool audioAvailable) {
  g = Display::graphics();


  audioReady = audioAvailable;
  // A warm reset can retain the clock before Wi-Fi returns.
  setenv("TZ", TIMEZONE, 1);
  tzset();
  Alarms::begin();
  alarmSchedulerRunning = Alarms::startScheduler();
  if (!alarmSchedulerRunning) Serial.println("Alarms: using main-loop scheduler");
  Turn *sessionTurns = static_cast<Turn *>(ps_malloc(sizeof(Turn)*32));
  ChatLine *sessionLines = static_cast<ChatLine *>(ps_malloc(sizeof(ChatLine)*4096));
  if (sessionTurns && sessionLines) {
    turns = sessionTurns; lines = sessionLines; turnCapacity = 32; lineCapacity = 4096;
    memset(turns, 0, sizeof(Turn)*turnCapacity);
  } else { free(sessionTurns); free(sessionLines); }
  artworkPixels = static_cast<uint16_t *>(ps_malloc(MusicArtwork::SIDE*MusicArtwork::SIDE*2));
  if (!artworkPixels) artworkPixels = static_cast<uint16_t *>(malloc(MusicArtwork::SIDE*MusicArtwork::SIDE*2));
  if (!artworkPixels) Serial.println("UI artwork buffer unavailable; using icon");
  DeviceSettings::begin(audioReady);

  audio = Sound::snapshot();
  music = Music::snapshot();
  assistant = CompanionAssistant::snapshot();
  routeSpeech(assistant.busy);
  Face::setVisible(true);
  g->fillScreen(BACKGROUND);
  dirty = headerDirty = true;
  update(millis());
  startupUntil=millis()+1500; lastActivity=millis();
  showStartup("Starting Pod / connecting...");
  Serial.println("UI ready: 480x320 landscape / Home Music Chat Settings");
}
void touch(uint16_t x, uint16_t y, bool pressed, uint32_t now) {
  if (shutdownAt || startupUntil) return;
  if (pressed) { lastActivity=now; Face::setSleeping(false); }
  if (pressed && !touching) {

    touching = true; moved = false;
    fingerX = x; fingerY = y;

    startX = x; startY = previousY = y; dragRemainder = 0;
    if (inside(x,y,0,0,ClockW,HEADER_HEIGHT)) headerDirty = true;
    if (page == Page::Alarms && editingAlarm) alarmEditorDirty |= alarmStepRegion(startX,startY);
    else if (page == Page::Ringing) dirty = true;
    if (page==Page::Home && homeListening() && inside(x,y,HOME_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE)) {
      cancelHomeListening();
      moved=true; // Consume the release even though the Chat icon is back.
    }
    if (page == Page::Music)
      for (int i=0;i<4;++i) if (inside(x,y,transportX(i),TRANSPORT_Y,transportWidth(i),TRANSPORT_SIDE))
        musicButtonsDirty |= uint8_t(1U<<i);
    if (page == Page::Chat) markChatControls(x,y);
    if (page == Page::Settings && (inside(x,y,154+settingsShift(),118,220,31) || inside(x,y,154+settingsShift(),211,220,31) ||
        inside(x,y,154+settingsShift(),260,220,33))) dirty = true;
    sliderDrag = sliderHit(x, y);
    chatDrag = page == Page::Chat && inside(x,y,12,79,contentWidth()-78,171);
    if (sliderDrag) adjustSlider(x);
    if (page==Page::Home && audio.route==Sound::Route::Idle && y>=HEADER_HEIGHT &&
        !inside(x,y,LEFT_SHORTCUT_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE) &&
        !inside(x,y,HOME_X,SHORTCUT_Y,SHORTCUT_SIZE,SHORTCUT_SIZE))
      Sound::play(Sound::Effect::Touch);
  } else if (pressed && touching) {

    const bool previousMove = moved;
    const bool wasTransport = page == Page::Music && (
        held(transportX(0),TRANSPORT_Y,transportWidth(0),TRANSPORT_SIDE) || held(transportX(1),TRANSPORT_Y,transportWidth(1),TRANSPORT_SIDE) ||
        held(transportX(2),TRANSPORT_Y,transportWidth(2),TRANSPORT_SIDE) || held(transportX(3),TRANSPORT_Y,transportWidth(3),TRANSPORT_SIDE));
    fingerX = x; fingerY = y;
    if (abs(int(x) - startX) > 12 || abs(int(y) - startY) > 12) moved = true;
    const bool isTransport = page == Page::Music && (
        held(transportX(0),TRANSPORT_Y,transportWidth(0),TRANSPORT_SIDE) || held(transportX(1),TRANSPORT_Y,transportWidth(1),TRANSPORT_SIDE) ||
        held(transportX(2),TRANSPORT_Y,transportWidth(2),TRANSPORT_SIDE) || held(transportX(3),TRANSPORT_Y,transportWidth(3),TRANSPORT_SIDE));
    if (previousMove != moved) {
      if (inside(startX,startY,0,0,ClockW,HEADER_HEIGHT)) headerDirty = true;
      if (page == Page::Alarms && editingAlarm) alarmEditorDirty |= alarmStepRegion(startX,startY);
      else if (page == Page::Ringing) dirty = true;
    }
    if (previousMove != moved || wasTransport != isTransport) {
      if (page == Page::Music && (wasTransport || isTransport)) {
        for (int i=0;i<4;++i) if (inside(startX,startY,transportX(i),TRANSPORT_Y,transportWidth(i),TRANSPORT_SIDE))
          musicButtonsDirty |= uint8_t(1U<<i);
      }

    }
    if (sliderDrag) adjustSlider(x);
    if (chatDrag && moved) {
      dragRemainder += int(previousY) - y;
      if (abs(dragRemainder) >= LINE_HEIGHT) {
        const int next = constrain(scrollTop + dragRemainder / LINE_HEIGHT,
                                   0, max(0, lineCount - VISIBLE_LINES));
        if (next < scrollTop) followLatest = false;
        scrollTop = next;
        dragRemainder %= LINE_HEIGHT;
        chatRegionDirty = true;
      }
    }
    previousY = y;
  } else if (!pressed && touching) {
    touching = false;
    if (inside(startX,startY,0,0,ClockW,HEADER_HEIGHT)) headerDirty = true;
    if (page == Page::Alarms && editingAlarm) alarmEditorDirty |= alarmStepRegion(startX,startY);
    else if (page == Page::Ringing) dirty = true;
    if (page == Page::Music)
      for (int i=0;i<4;++i) if (inside(startX,startY,transportX(i),TRANSPORT_Y,transportWidth(i),TRANSPORT_SIDE))
        musicButtonsDirty |= uint8_t(1U<<i);
    if (page == Page::Chat) markChatControls(startX,startY);

    if (!moved && !sliderDrag) tap(startX, startY);
    sliderDrag = chatDrag = false;
  }
  if (pressed && page == Page::Home && x < contentWidth()) Face::onTouch(x, y, now);
}
void update(uint32_t now) {

  if (startupUntil) {
    if (int32_t(now-startupUntil)<0) return;
    startupUntil=0; dirty=headerDirty=true;
  }
  if (shutdownAt) {
    if (uint32_t(now-shutdownAt)>=1800) {
      if (Alarms::hasArmedAlarm()) {
        shutdownAt = 0;
        showNotice("Power off canceled: alarm armed");
        return;
      }
      DeviceSettings::flush();
      if (!audioReady || Sound::prepareSleep()) {
        Display::backlight(false);
        LedRing::setTheme(0,6);
        for (int i=0;i<30;++i) { LedRing::update(now+i*30,Face::Expression::Idle); }
        esp_deep_sleep_start();
      } else {
        shutdownAt=0;
        Sound::releaseAssistant();
        showNotice("Audio is busy; power off canceled");
      }
    }
    return;
  }
  refreshAlarms(now);
  DeviceSettings::update(now, touching);
  remoteUpdate(now);
  if (shutdownAt) return;
  const bool active=assistant.busy || assistant.replyReady || audio.route!=Sound::Route::Idle || audio.teaching || audio.wakeTest==1;
  if (active || conversation.active()) lastActivity=now;
  const int requestedVolume = CompanionAssistant::takeVolumeRequest();
  if (requestedVolume >= 0) {
    CompanionAssistant::finishVolumeRequest(DeviceSettings::setVolume(requestedVolume));
    if (page == Page::Settings && category == 2) sliderRegionDirty = true;
  }
  if ((page==Page::Chat || page==Page::Settings) && !touching && !active && uint32_t(now-lastActivity)>=120000)
    select(Page::Home);
  if (assistant.replyReady && !homeCanceling && !replyPlaybackPending && audio.route==Sound::Route::Idle &&
      CompanionAssistant::playReadyReply()) {
    replyPlaybackPending = true;
    routeSpeech(true);
    Sound::setFollowUpListening(false);
    if (page == Page::Home) homeTextDirty = true;
  }
  const bool sleeping=page==Page::Home && !touching && !active && !replyPlaybackPending &&
                      uint32_t(now-lastActivity)>=HOME_SLEEP_MS;
  Face::setSleeping(sleeping);
  if (noSpeechAt && uint32_t(now-noSpeechAt)>=NO_SPEECH_NOTICE_MS) {
    noSpeechAt=0;
    homeTextDirty=true;
  }
  const int key=Buttons::update(now);
  if (key>=0 && key<3) {
    const Page destinations[]={Page::Home,Page::Music,Page::Chat};
    select(destinations[key]);
  }
  const int hour=localHour();
  if (page==Page::Home && greetingHour!=hour) {
    greetingHour=hour;
    homeTextDirty=true;
  }
  if (flashTransport >= 0 && uint32_t(now-flashAt) >= 160) {
    flashTransport = -1;
    if (page == Page::Music) musicButtonsDirty |= 15;
  }
  if (notice[0] && uint32_t(now - noticeAt) >= 2500) { notice[0] = 0; dirty = true; }
  if (!lastPoll || uint32_t(now - lastPoll) >= 200) {
    lastPoll = now;
    const auto nextAudio = Sound::snapshot();
    const auto nextMusic = Music::snapshot();
    if (nextAudio.wakeCount!=seenWake) {
      seenWake=nextAudio.wakeCount;
      conversation.start();
      CompanionAssistant::setConversation(true, DeviceSettings::values().wakeEachTurn);
      if (page==Page::Home && nextAudio.wakeOrigin==Sound::VoicePage::Home) { noSpeechAt=0; homeTextDirty=true; lastActivity=now; }
    }
    const auto completed=Sound::takeVoiceRecording();
    if (completed != Sound::VoicePage::Off && !homeCanceling) {
      Serial.printf("Voice handoff: loop stack margin=%u, free heap=%u\n",
                    unsigned(uxTaskGetStackHighWaterMark(nullptr)), ESP.getFreeHeap());
      if (!CompanionAssistant::askLast(completed == Sound::VoicePage::Music, conversation.answeringPrompt(),
                                         page == Page::Home))
        showNotice("Voice could not start processing");
    }
    const auto nextAssistant = CompanionAssistant::snapshot();
    if (nextAudio.replyPlaybackEvents != audio.replyPlaybackEvents) replyPlaybackPending = false;
    const bool turnBusy = nextAssistant.busy || nextAssistant.replyReady || replyPlaybackPending ||
                          nextAudio.route != Sound::Route::Idle;
    if (promptQueued && !turnBusy) promptQueued = false;
    const bool canceled = homeCanceling;
    if (canceled && !turnBusy) homeCanceling = false;
    if (nextAssistant.acceptedTurns != assistant.acceptedTurns) conversation.acceptedTurn();
    const auto &voiceSettings = DeviceSettings::values();
    const bool dismissed = nextAssistant.standbyRequests != assistant.standbyRequests;
    const auto event = dismissed ? Conversation::Event::Standby :
        conversation.update(now, turnBusy || promptQueued, voiceSettings.wakeEachTurn, voiceSettings.followUpPrompt);
    if (event == Conversation::Event::Prompt) {
      Sound::setFollowUpListening(false);
      promptQueued = CompanionAssistant::sayFollowUp(page == Page::Home);
      if (!promptQueued) showNotice("Do you need anything else?");
    } else if (event == Conversation::Event::Standby) {
      conversation.stop();
      Sound::setFollowUpListening(false);
      CompanionAssistant::setConversation(false);
      homeTextDirty = true;
      Serial.println("Conversation: standby");
    }
    if (nextAssistant.musicNavigation != seenMusicNavigation) {
      seenMusicNavigation = nextAssistant.musicNavigation;
      if (!canceled && (page==Page::Home || page==Page::Chat)) select(Page::Music);
    }
    if (nextAssistant.chatNavigation != seenChatNavigation) {
      seenChatNavigation = nextAssistant.chatNavigation;
      if (!canceled && page==Page::Home) select(Page::Chat);
    }
    routeSpeech(nextAssistant.busy || nextAssistant.replyReady || replyPlaybackPending || promptQueued ||
                nextAudio.route==Sound::Route::AssistantPlayback);
    if (page==Page::Music && (audio.route!=nextAudio.route || strcmp(audio.message,nextAudio.message) ||
        nextAssistant.revision!=assistant.revision)) { musicVoiceDirty=true; musicButtonsDirty|=8; }
    if (captureConversation(nextAssistant) && page == Page::Chat) chatRegionDirty = true;
    if (page == Page::Home && !canceled && nextAssistant.revision != assistant.revision &&
        !strcmp(nextAssistant.status, "Local reply") && nextAssistant.reply[0])
      showNotice(nextAssistant.reply);
    if (page == Page::Music) {
      const bool trackChanged = strcmp(music.videoId,nextMusic.videoId) != 0;
      if (trackChanged) musicArtworkDirty = true;
      if (trackChanged || strcmp(music.title,nextMusic.title) || strcmp(music.artist,nextMusic.artist))
        musicDetailsDirty = true;
      if (music.online != nextMusic.online || music.trackState != nextMusic.trackState) {
        musicButtonsDirty |= 15;
        dotPhase = -1;
      }
      if (music.online != nextMusic.online || music.trackState != nextMusic.trackState ||
          music.adPlaying != nextMusic.adPlaying || music.progressAvailable != nextMusic.progressAvailable ||
          music.durationMs != nextMusic.durationMs || trackChanged)
        musicProgressDirty = true;
    }
    if (page == Page::Chat && (
        audio.route != nextAudio.route || audio.hasRecording != nextAudio.hasRecording ||
        audio.microphoneReady != nextAudio.microphoneReady || audio.teaching!=nextAudio.teaching || audio.wakeTrained!=nextAudio.wakeTrained ||

        strcmp(audio.message, nextAudio.message) || strcmp(assistant.status, nextAssistant.status) ||
        assistant.busy != nextAssistant.busy || assistant.configured != nextAssistant.configured)) chatStateDirty = true;
    if (page==Page::Chat && audio.recordedMs/100 != nextAudio.recordedMs/100) chatTimerDirty=true;

    if (page == Page::Home) {
      const bool captionChanged = nextAudio.route == Sound::Route::AssistantPlayback &&
          HomeCaptions::at(nextAssistant.reply, nextAudio.replyPlayedFrames, nextAudio.replyTotalFrames) !=
          HomeCaptions::at(assistant.reply, audio.replyPlayedFrames, audio.replyTotalFrames);
      if (canceled || audio.route != nextAudio.route || assistant.busy != nextAssistant.busy ||
          assistant.replyReady != nextAssistant.replyReady || assistant.visible != nextAssistant.visible ||
          strcmp(assistant.transcript, nextAssistant.transcript) || captionChanged)
        homeTextDirty = true;
      if (!canceled && nextAudio.wakeTimeouts != audio.wakeTimeouts &&
          nextAudio.wakeOrigin == Sound::VoicePage::Home && nextAudio.wakeCount != canceledWake) {
        noSpeechAt = now;
        homeTextDirty = true;
      }
    }
    audio = nextAudio; music = nextMusic; assistant = nextAssistant;
    Sound::setFollowUpListening(conversation.listening() && page < Page::Settings && !promptQueued && !replyPlaybackPending &&
        !audio.teaching && audio.wakeTest != 1 && !assistant.busy && !assistant.replyReady &&
        audio.route == Sound::Route::Idle && CompanionNetwork::connected());
    Face::setVoiceActivity(page==Page::Home && homeListening(),
                           page==Page::Home && audio.route==Sound::Route::AssistantPlayback);
    Face::setSinging(music.online && music.trackState == 1 && !music.adPlaying);
    const auto nextArtwork = MusicArtwork::snapshot();
    if (page == Page::Music && artwork.revision != nextArtwork.revision) {
      musicArtworkDirty = musicProgressDirty = true; musicButtonsDirty |= 15;
    }
    artwork = nextArtwork;
    if (artworkPixels && matchingArtwork() && copiedArtwork != artwork.revision &&
        MusicArtwork::copyPixels(music.videoId,artwork.revision,artworkPixels)) {
      copiedArtwork = artwork.revision;
      if (page == Page::Music) musicArtworkDirty = true;
    }
    const bool connected = CompanionNetwork::connected();
    if (connected != wasConnected) {
      wasConnected = connected; headerDirty = true; lastInfo = 0;
      if (page==Page::Home) homeTextDirty=true;
      if (page == Page::Settings && category == 1) dirty = true;
    }
    if (connected && !clockStarted) {
      configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
      clockStarted = true;
    }
    if (!lastInfo || uint32_t(now - lastInfo) >= 5000) {
      lastInfo = now;
      const String nextSsid = connected ? WiFi.SSID() : "";
      const String nextAddress = connected ? WiFi.localIP().toString() : "";
      const int nextSignal = connected ? WiFi.RSSI() : 0;
      if ((nextSsid != ssid || nextAddress != address || nextSignal != signalDb) &&
          (page == Page::Settings && category == 1)) dirty = true;
      ssid = nextSsid; address = nextAddress; signalDb = nextSignal;
    }
    header();
  }
  LedRing::setMusicAccent(musicColor(),matchingArtwork());
  LedRing::setSpeaking(audio.route==Sound::Route::AssistantPlayback);
  LedRing::setTheme(page >= Page::Alarms ? 0 : uint8_t(page), DeviceSettings::values().palette);
  LedRing::setRecording(audio.route == Sound::Route::Recording);
  if (headerDirty) header();
  if (dirty) {
    drawPage();
    alarmEditorDirty = 0;
    dirty = false; musicButtonsDirty = 0; sliderRegionDirty = false; chatRegionDirty = false;
    musicArtworkDirty = musicDetailsDirty = musicProgressDirty = false;
    chatStateDirty=chatTimerDirty=chatScrollDirty=false; chatControlsDirty=0; homeTextDirty=false;
  } else {
    if (page == Page::Alarms && editingAlarm && alarmEditorDirty) {
      alarmEditorRegions(alarmEditorDirty);
      alarmEditorDirty = 0;
    }
    if (page == Page::Music) {
      if (musicVoiceDirty && !notice[0]) musicVoiceStatus();
      if (musicArtworkDirty) musicArtworkPanel();
      if (musicDetailsDirty) musicDetails();
      else if (musicProgressDirty) musicProgress(now);
      for (int i=0;i<4;++i) if (musicButtonsDirty & (1U<<i)) transportButtons(i);
    }
    musicArtworkDirty = musicDetailsDirty = musicProgressDirty = false;
    musicButtonsDirty = 0;
    if (page == Page::Settings && sliderRegionDirty) {
      g->fillRect(151+settingsShift(),112,224,94,PANEL);
      const auto &v = DeviceSettings::values();
      char value[32];
      if (category == 0) snprintf(value,sizeof(value),"%u%%",v.brightness);
      else if (category == 2) snprintf(value,sizeof(value),"%u%%",v.volume);
      else snprintf(value,sizeof(value),"%+d dB",v.microphoneDb);
      text(value,154+settingsShift(),117,TEXT,Font::Heading);
      if (category == 0) slider(v.brightness,10,100);
      else if (category == 2) slider(v.volume,0,100);
      else if (category == 3) slider(v.microphoneDb,-12,12,PURPLE);
    }
    sliderRegionDirty = false;
    if (page == Page::Chat && chatRegionDirty) {
      chatConversation(); chatJump(); chatScrollButtons();
    }
    if (page==Page::Chat) {
      if (chatStateDirty) { chatStatus(); chatTimer(); chatControls(); }
      else {
        if (chatTimerDirty) chatTimer();
        for (int i=0;i<2;++i) if (chatControlsDirty & (1U<<i)) chatControls(i);
      }
      if (chatScrollDirty) { chatJump(); chatScrollButtons(); }
    }
    if (page==Page::Home && homeTextDirty) {
      if (!notice[0]) homeText();
      homeRightShortcut();
    }
    homeTextDirty=false; chatStateDirty=chatTimerDirty=chatScrollDirty=false; chatControlsDirty=0;
    chatRegionDirty = false;
  }
  if (page == Page::Music && !notice[0] && dotPhase != playbackPhase(now)) playingDots(now);
  if (page == Page::Music && music.progressAvailable && music.durationMs &&
      Music::positionMsAt(music,now)/1000 != progressSecond) musicProgress(now);
  musicVoiceDirty=false;
  ConversationStatus::draw(g, BACKGROUND, TEXT,
      page == Page::Ringing ? "Alarm" :
      page == Page::Home && homeCanceling ? "Standby" :
      audio.route == Sound::Route::Recording ? "Listening" :
      audio.route == Sound::Route::AssistantPlayback ? (page == Page::Home ? "Replying" : "Speaking") :
      assistant.busy || assistant.replyReady || promptQueued ? "Thinking" :
      conversation.active() ? "Listening" : "Standby");
}
}

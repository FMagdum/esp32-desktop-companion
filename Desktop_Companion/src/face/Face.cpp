#include "Face.h"
#include "../display/Display.h"
#include "../config/UIConfig.h"

namespace {
class MovableCanvas : public Arduino_Canvas {
public:
  using Arduino_Canvas::Arduino_Canvas;
  void setPosition(int x, int y) { _output_x = x; _output_y = y; }
};
MovableCanvas *canvas = nullptr;
int contentWidth = UIConfig::CONTENT_WIDTH;
bool needsDraw = true;
bool ready = false, visible = false, happy = false, blinking = false, singing = false, sleeping = false;
bool listening = false, speaking = false;
int mouthFrame = -1;
uint32_t lastFrame = 0, lastTouch = 0, lastGaze = 0;
uint32_t lastBlink = 0, blinkStart = 0, blinkInterval = 2500;
int gazeX = 0, gazeY = 0, targetX = 0, targetY = 0;
int speakingFrame(uint32_t now) {
  return (now / 100) % 6;
}
int approach(int value, int target) {
  return value + (value < target ? 1 : value > target ? -1 : 0);
}
void draw(uint32_t now) {
  if (!ready || !visible) return;
  using namespace UIConfig;
  canvas->fillScreen(BACKGROUND);
  // Ears and a five-pixel outline echo the supplied reference.
  const int earWidth=listening ? 49 : 32, earHeight=listening ? 86 : 64;
  canvas->fillRoundRect(54-earWidth,84-earHeight/2,earWidth,earHeight,earWidth/2,CYAN);
  canvas->fillRoundRect(306,84-earHeight/2,earWidth,earHeight,earWidth/2,CYAN);
  canvas->fillRoundRect(37,4,286,181,57,CYAN);
  canvas->fillRoundRect(42,9,276,171,52,BACKGROUND);
  int height = 38;
  if (blinking) {
    const uint32_t elapsed = now - blinkStart;
    height = elapsed < 100 ? 38 - elapsed * 35 / 100
                          : 3 + (elapsed - 100) * 35 / 100;
    height = constrain(height, 3, 38);
  }
  for (int center : {121, 239}) {
    if (sleeping) {
      canvas->fillRoundRect(center - 18, 62, 36, 4, 2, TEXT);
    } else if (!listening && !speaking && (happy || (singing && !blinking))) {
      canvas->fillCircle(center, 61, 21, TEXT);
      canvas->fillCircle(center, 67, 17, BACKGROUND);
      canvas->fillRect(center - 22, 67, 45, 23, BACKGROUND);
      canvas->fillRoundRect(center - 19, 88, 4, 9, 2, CYAN);
      canvas->fillRoundRect(center - 10, 88, 4, 9, 2, CYAN);
    } else {
      // Eye bounds: y=30..79 including gaze. Mouth mask starts at y=88.
      canvas->fillRoundRect(center - 18 + gazeX, 65 - height / 2 + gazeY,
                           36, height, min(12, height / 2), TEXT);
    }
  }
  if (speaking) {
    constexpr int openings[]={12,24,32,19,29,13};
    mouthFrame=speakingFrame(now);
    const int opening=openings[mouthFrame], y=118-opening/2;
    canvas->fillRoundRect(157,y,46,opening,min(14,opening/2),CYAN);
    canvas->fillRoundRect(162,y+4,36,opening-8,min(9,(opening-8)/2),BACKGROUND);
  } else if (happy && !listening) {
    canvas->fillCircle(180, 118, 19, CYAN);
    canvas->fillRect(160, 98, 41, 20, BACKGROUND);
    canvas->fillRoundRect(161, 115, 39, 5, 2, CYAN);
  } else if (sleeping) {
    canvas->drawCircle(180, 118, 6, CYAN);
  } else if (singing && !listening) {
    // A gentle timed singing expression, independent of track audio. Keep its
    // top below the eye area even when gaze is at the lowest point.
    const uint32_t phase = (now % 800);
    const uint32_t wave = phase < 400 ? phase : 800 - phase;
    const int rx = 10 + wave * 4 / 400, ry = 7 + wave * 7 / 400;
    canvas->fillEllipse(180, 118, rx, ry, CYAN);
  } else {
    canvas->fillCircle(180, 118, 20, CYAN);
    canvas->fillCircle(180, 112, 17, BACKGROUND);
    canvas->fillRect(159, 89, 43, 29, BACKGROUND);
  }
  canvas->flush();
  needsDraw = false;
}
}
namespace Face {
Expression expression() {
  return happy ? Expression::Happy : blinking ? Expression::Blink : Expression::Idle;
}
bool begin() {
  static MovableCanvas faceCanvas(UIConfig::FACE_WIDTH, UIConfig::FACE_HEIGHT,
       Display::graphics(), UIConfig::FACE_X, 48);
  canvas = &faceCanvas;
  ready = canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  lastFrame = lastBlink = lastGaze = millis();
  return ready;
}
void setVisible(bool value) { visible = value; needsDraw = true; }
void setLayout(int width) {
  contentWidth = width;
  if (canvas) canvas->setPosition((width - UIConfig::FACE_WIDTH) / 2, 48);
  needsDraw = true;
}
void setSinging(bool value) {
  if (singing == value) return;
  singing = value;
  if (!sleeping) needsDraw = true;
  Serial.println(singing ? "Face: singing" : "Face: music stopped");
}
void setVoiceActivity(bool hearing, bool talking) {
  if (listening==hearing && speaking==talking) return;
  listening=hearing;
  speaking=talking;
  mouthFrame=-1;
  needsDraw=true;
}
void setSleeping(bool value) {
  if (sleeping == value) return;
  sleeping = value;
  happy = blinking = false;
  gazeX = gazeY = targetX = targetY = 0;
  lastBlink = lastGaze = millis();
  needsDraw = true;
}
void redraw() { draw(millis()); }
void onTouch(uint16_t x, uint16_t y, uint32_t now) {
  if (!ready || !visible || x >= contentWidth ||
      y < UIConfig::HEADER_HEIGHT || y >= UIConfig::HEIGHT) return;
  setSleeping(false);
  if (!happy) Serial.println("Face: happy");
  if (!happy) needsDraw = true;
  happy = true;
  lastTouch = now;
  targetX = map(x, 0, contentWidth - 1, -7, 7);
  targetY = map(y, UIConfig::HEADER_HEIGHT, UIConfig::HEIGHT - 1, -5, 5);
}
void update(uint32_t now) {
  if (!ready || !visible || uint32_t(now - lastFrame) < 50) return;
  lastFrame = now;
  if (sleeping) {
    if (needsDraw) draw(now);
    return;
  }
  const bool wasHappy = happy, wasBlinking = blinking;
  const int oldX = gazeX, oldY = gazeY;
  if (happy && uint32_t(now - lastTouch) >= 1500) {
    happy = false;
    lastBlink = now;
    Serial.println(singing ? "Face: singing" : "Face: idle");
  }
  if (happy) blinking = false;
  else {
    if (!blinking && uint32_t(now - lastBlink) >= blinkInterval) {
      blinking = true;
      blinkStart = now;
    }
    if (blinking && uint32_t(now - blinkStart) >= 200) {
      blinking = false;
      lastBlink = now;
      blinkInterval = random(2500, 5501);
    }
    if (uint32_t(now - lastGaze) >= 2000) {
      lastGaze = now;
      targetX = random(-7, 8);
      targetY = random(-5, 6);
    }
  }
  gazeX = approach(gazeX, targetX);
  gazeY = approach(gazeY, targetY);
  if (needsDraw || (speaking && mouthFrame!=speakingFrame(now)) ||
      (singing && !speaking && !listening) || blinking || wasBlinking != blinking ||
      wasHappy != happy || oldX != gazeX || oldY != gazeY) draw(now);
}
}

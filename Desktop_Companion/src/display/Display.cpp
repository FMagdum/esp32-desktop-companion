#include "Display.h"
#include "../config/BoardPins.h"
#include "../config/UIConfig.h"
namespace {
Arduino_DataBus *bus = new Arduino_ESP32SPI(
  BoardPins::LCD_DC, BoardPins::LCD_CS, BoardPins::LCD_SCLK,
  BoardPins::LCD_MOSI, BoardPins::LCD_MISO, FSPI);
Arduino_GFX *gfx = new Arduino_ST7796(bus, GFX_NOT_DEFINED, 0, true,
                                    BoardPins::LCD_WIDTH, BoardPins::LCD_HEIGHT);
uint8_t level = 80;
bool enabled = false, pwm = false;
bool apply() {
  const uint8_t duty = enabled ? uint16_t(level) * 255 / 100 : 0;
  if (pwm) return ledcWrite(BoardPins::LCD_BL, duty);
  digitalWrite(BoardPins::LCD_BL, enabled ? HIGH : LOW);
  return !enabled || level == 100;
}
}
namespace Display {
bool begin() {
  pinMode(BoardPins::LCD_BL, OUTPUT);
  digitalWrite(BoardPins::LCD_BL, LOW);
  if (!gfx->begin(20000000)) return false;
  gfx->setRotation(UIConfig::ROTATION);
  pwm = ledcAttach(BoardPins::LCD_BL, 5000, 8);
  if (!pwm) {
    level = 100;
    Serial.println("Backlight PWM unavailable; using full brightness");
  }
  apply();
  return true;
}
void backlight(bool value) { enabled = value; apply(); }
bool setBrightness(uint8_t percent) {
  if (!pwm) return false;
  const uint8_t previous = level;
  level = constrain(percent, 10, 100);
  if (apply()) return true;
  level = previous;
  return false;
}
uint8_t brightness() { return level; }
Arduino_GFX *graphics() { return gfx; }
}

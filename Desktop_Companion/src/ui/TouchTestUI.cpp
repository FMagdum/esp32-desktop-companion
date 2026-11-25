#include "TouchTestUI.h"
#include "../display/Display.h"
#include "../config/BoardPins.h"

namespace {
int previousX = -1;
int previousY = -1;
}

namespace TouchTestUI {
void draw() {
  auto *gfx = Display::graphics();
  gfx->fillScreen(RGB565::BLACK);
  gfx->setTextColor(RGB565::CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(48, 18);
  gfx->println("TOUCH TEST");
  gfx->drawFastHLine(0, 55, BoardPins::LCD_WIDTH, RGB565::CYAN);
  gfx->setTextColor(RGB565::WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 75);
  gfx->println("X: ---  Y: ---");
  gfx->setTextSize(1);
  gfx->setCursor(60, 115);
  gfx->println("Touch anywhere below");
  gfx->drawRect(1, 140, 318, 338, RGB565::WHITE);
}

void showTouch(uint16_t x, uint16_t y) {
  auto *gfx = Display::graphics();
  gfx->fillRect(0, 70, BoardPins::LCD_WIDTH, 30, RGB565::BLACK);
  gfx->setTextColor(RGB565::GREEN);
  gfx->setTextSize(2);
  gfx->setCursor(20, 75);
  gfx->print("X:"); gfx->print(x);
  gfx->setCursor(170, 75);
  gfx->print("Y:"); gfx->print(y);

  // Previous marker is erased without clearing the entire display.
  if (previousX >= 9 && previousY >= 149 &&
      previousX <= BoardPins::LCD_WIDTH - 10 &&
      previousY <= BoardPins::LCD_HEIGHT - 10) {
    gfx->fillCircle(previousX, previousY, 7, RGB565::BLACK);
  }

  if (x >= 9 && x < BoardPins::LCD_WIDTH - 9 &&
      y >= 149 && y < BoardPins::LCD_HEIGHT - 9) {
    gfx->fillCircle(x, y, 6, RGB565::CYAN);
    previousX = x;
    previousY = y;
  } else {
    previousX = previousY = -1;
  }
}

void showTouchError() {
  auto *gfx = Display::graphics();
  gfx->setTextColor(RGB565::RED);
  gfx->setTextSize(2);
  gfx->setCursor(15, 200);
  gfx->println("Touch not found!");
}
}

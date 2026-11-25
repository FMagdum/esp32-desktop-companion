#include "Touch.h"
#include <Wire.h>
#include "../config/BoardPins.h"
#include "../config/UIConfig.h"

namespace Touch {
bool begin() {
  Wire.beginTransmission(BoardPins::FT6336_ADDR);
  return Wire.endTransmission() == 0;
}

bool read(Point &point) {
  point = {};
  Wire.beginTransmission(BoardPins::FT6336_ADDR);
  Wire.write(0x02); // TD_STATUS register
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BoardPins::FT6336_ADDR, (uint8_t)5) != 5) return false;

  uint8_t data[5];
  for (uint8_t i = 0; i < 5; ++i) data[i] = Wire.read();

  const uint8_t count = data[0] & 0x0F;
  if (count == 0 || count > 2) return true;
  const uint8_t event = (data[1] >> 6) & 0x03;
  if (event == 1) return true; // finger-up event

  point.x = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
  point.y = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];

  // Reject corrupt raw coordinates before unsigned subtraction.
  if (point.x >= BoardPins::LCD_WIDTH || point.y >= BoardPins::LCD_HEIGHT)
    return true;
  const uint16_t rawX = point.x, rawY = point.y;
  if (UIConfig::ROTATION == 1) {
    point.x = rawY;
    point.y = BoardPins::LCD_WIDTH - 1 - rawX;
  } else if (UIConfig::ROTATION == 3) {
    point.x = BoardPins::LCD_HEIGHT - 1 - rawY;
    point.y = rawX;
  }
  point.count = count;
  return true;
}
}

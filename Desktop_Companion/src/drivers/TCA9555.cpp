#include "TCA9555.h"
#include <Wire.h>
#include "../config/BoardPins.h"

namespace TCA9555 {
bool readRegister(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(BoardPins::TCA9555_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BoardPins::TCA9555_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BoardPins::TCA9555_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initDisplayAndTouchResets() {
  uint8_t oldOutput = 0;
  uint8_t oldConfig = 0;
  if (!readRegister(0x02, oldOutput)) return false; // output port 0
  if (!readRegister(0x06, oldConfig)) return false; // config port 0

  constexpr uint8_t mask = BoardPins::LCD_RST_MASK | BoardPins::TOUCH_RST_MASK;
  // Set output HIGH before changing direction to avoid unwanted low pulses.
  if (!writeRegister(0x02, oldOutput | mask)) return false;
  if (!writeRegister(0x06, oldConfig & (uint8_t)~mask)) return false;
  delay(20);
  if (!writeRegister(0x02, oldOutput & (uint8_t)~mask)) return false;
  delay(30);
  if (!writeRegister(0x02, oldOutput | mask)) return false;
  delay(350);
  return true;
}
}

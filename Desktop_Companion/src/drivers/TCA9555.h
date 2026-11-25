#pragma once
#include <Arduino.h>

namespace TCA9555 {
bool readRegister(uint8_t reg, uint8_t &value);
bool writeRegister(uint8_t reg, uint8_t value);
bool initDisplayAndTouchResets();
}

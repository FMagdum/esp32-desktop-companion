#pragma once
#include <Arduino.h>

namespace Touch {
struct Point {
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t count = 0;
};

bool begin();
// Logical landscape coordinates, matching Display rotation.
// Returns false on I2C failure; true on a valid poll (count 0 if no touch).
bool read(Point &point);
}

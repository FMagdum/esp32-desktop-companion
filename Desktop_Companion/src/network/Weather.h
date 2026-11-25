#pragma once
#include <Arduino.h>
namespace Weather {
struct Snapshot {
  bool available = false, stale = false;
  char text[64] = "Weather awaiting connection";
  uint32_t revision = 0;
};
void begin();
void setLocation(double latitude, double longitude);
Snapshot snapshot();
}


#include "Buttons.h"
#include "TCA9555.h"

namespace {
constexpr uint8_t KEYS = 0x0e; // EXIO9..11, input port 1 bits 1..3.
bool ready = false;
uint8_t candidate = 0, stable = 0;
uint32_t lastPoll = 0, changedAt[3]{};
}
namespace Buttons {
bool begin() {
  uint8_t config, inputs;
  if (!TCA9555::readRegister(0x07, config) ||
      !TCA9555::writeRegister(0x07, config | KEYS) ||
      !TCA9555::readRegister(0x01, inputs)) return false;
  candidate = stable = uint8_t(~inputs) & KEYS;
  for (auto &changed : changedAt) changed = millis();
  ready = true;
  return true;
}
int update(uint32_t now) {
  if (!ready || uint32_t(now-lastPoll)<20) return -1;
  lastPoll = now;
  uint8_t inputs;
  if (!TCA9555::readRegister(0x01, inputs)) {
    for (auto &changed : changedAt) changed = now;
    return -1;
  }
  const uint8_t pressed = uint8_t(~inputs) & KEYS;
  int event = -1;
  for (int i=0; i<3; ++i) {
    const uint8_t bit = 1U << (i+1);
    if ((pressed & bit) != (candidate & bit)) {
      candidate ^= bit;
      changedAt[i] = now;
    }
    if ((candidate & bit) != (stable & bit) && uint32_t(now-changedAt[i])>=40) {
      stable ^= bit;
      if (stable & bit) event = i;
    }
  }
  return event;
}
}


#pragma once
#include <Arduino.h>
namespace Buttons {
bool begin();
// Returns Key1..Key3 as 0..2 once per debounced press, otherwise -1.
int update(uint32_t now);
}


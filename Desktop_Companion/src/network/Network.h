#pragma once
#include <Arduino.h>
namespace CompanionNetwork {
void begin(const char *ssid, const char *password);
bool beginFromStorage(bool storageAvailable);
bool connected();
void update(uint32_t now); // Main loop only.
}

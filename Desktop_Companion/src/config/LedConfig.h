#pragma once
#include <Adafruit_NeoPixel.h>

namespace LedConfig {
// Verify red/green order on hardware; switch to NEO_RGB if swapped.
constexpr neoPixelType PIXEL_TYPE = NEO_GRB + NEO_KHZ800;
constexpr uint8_t MAX_BRIGHTNESS = 32; // Channel ceiling out of 255.
}

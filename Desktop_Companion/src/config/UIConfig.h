#pragma once
#include <stdint.h>

namespace UIConfig {
// Native panel geometry and all GPIO definitions stay in BoardPins.h.
constexpr uint8_t ROTATION = 1;
constexpr int WIDTH = 480, HEIGHT = 320, CONTENT_WIDTH = 400, HEADER_HEIGHT = 34;
constexpr int FACE_X = 60, FACE_Y = 42, FACE_WIDTH = 360, FACE_HEIGHT = 186;
inline uint16_t BACKGROUND = 0x0842; // deep navy
inline uint16_t PANEL = 0x10C4, RAISED = 0x1926, BORDER = 0x2948;
inline uint16_t TEXT = 0xF7BE, MUTED = 0x9D16;
constexpr uint16_t CYAN = 0x36DF;
constexpr uint16_t BLUE = 0x04BD, GREEN = 0x4EB2, AMBER = 0xFDB0;
constexpr uint16_t PURPLE = 0xAC7F, RED = 0xFA6B;
inline void setLightMode(bool light) {
  BACKGROUND = light ? 0xeefb : 0x0842;
  PANEL = light ? 0xffff : 0x10c4;
  RAISED = light ? 0xded9 : 0x1926;
  BORDER = light ? 0xb5f7 : 0x2948;
  TEXT = light ? 0x1926 : 0xf7be;
  MUTED = light ? 0x52ec : 0x9d16;
}
constexpr const char *FIRMWARE = "Pod / settings 1.0";
constexpr const char *TIMEZONE = "IST-5:30"; // POSIX TZ: India, UTC+05:30
}

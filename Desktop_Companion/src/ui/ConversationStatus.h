#pragma once
#include <Arduino_GFX_Library.h>
namespace ConversationStatus {
void invalidate();
void draw(Arduino_GFX *graphics, uint16_t background, uint16_t foreground, const char *text);
}

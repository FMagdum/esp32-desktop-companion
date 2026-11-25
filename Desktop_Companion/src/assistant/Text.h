#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace AssistantText {
// Preserve character boundaries when a valid UTF-8 message exceeds capacity.
inline void copyUtf8(char *destination, size_t capacity, const char *source) {
  if (!capacity) return;
  size_t length = strnlen(source, capacity - 1);
  if (source[length]) {
    while (length && (static_cast<uint8_t>(source[length]) & 0xc0) == 0x80) --length;
  }
  memcpy(destination, source, length);
  destination[length] = 0;
}
}

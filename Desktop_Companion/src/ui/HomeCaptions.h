#pragma once
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace HomeCaptions {
struct Span {
  size_t offset = 0, length = 0;
  bool operator!=(Span other) const { return offset != other.offset || length != other.length; }
};

// Use the played fraction of the PCM clip to select a short group of words.
inline Span at(const char *text, uint32_t played, uint32_t total) {
  const auto length = strlen(text);
  if (!length || !total) return {};
  const size_t focus = size_t(uint64_t(played < total ? played : total) * (length - 1) / total);
  size_t cursor = 0;
  Span last{};
  while (cursor < length) {
    while (cursor < length && isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
    if (cursor == length) break;
    const size_t start = cursor;
    size_t end = start;
    for (unsigned words = 0; words < 7 && cursor < length; ++words) {
      while (cursor < length && !isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
      if (words && cursor - start > 45) { cursor = end; break; }
      end = cursor;
      while (cursor < length && isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
    }
    last = {start, end - start};
    if (focus < cursor) return last;
  }
  return last;
}
}

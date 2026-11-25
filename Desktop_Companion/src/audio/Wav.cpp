#include "Wav.h"
#include <string.h>
#include "../config/SoundConfig.h"

namespace {
uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
         uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

namespace Wav {
bool parse(fs::File &file, Info &info, uint32_t maxSeconds) {
  info = {};
  uint8_t header[16];
  if (!file.seek(0) || file.read(header, 12) != 12 ||
      memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) return false;
  const uint64_t end = uint64_t(le32(header + 4)) + 8;
  if (end < 12 || end > file.size()) return false;
  bool formatSeen = false;
  while (uint64_t(file.position()) + 8 <= end) {
    if (file.read(header, 8) != 8) return false;
    const uint32_t bytes = le32(header + 4);
    const uint32_t start = file.position();
    const uint64_t next = uint64_t(start) + bytes + (bytes & 1);
    if (next > end || next > UINT32_MAX) return false;
    if (!memcmp(header, "fmt ", 4)) {
      if (bytes < 16 || file.read(header, 16) != 16) return false;
      info.channels = le16(header + 2);
      const uint16_t align = info.channels * 2;
      if (le16(header) != 1 || (info.channels != 1 && info.channels != 2) ||
          le32(header + 4) != SoundConfig::SAMPLE_RATE ||
          le16(header + 14) != 16 || le16(header + 12) != align ||
          le32(header + 8) != SoundConfig::SAMPLE_RATE * align) return false;
      formatSeen = true;
    } else if (!memcmp(header, "data", 4)) {
      if (!formatSeen || !bytes || bytes % (info.channels * 2) ||
          bytes > SoundConfig::SAMPLE_RATE * info.channels * 2 * maxSeconds)
        return false;
      info.dataBytes = bytes;
      return true;
    }
    if (!file.seek(uint32_t(next))) return false;
  }
  return false;
}
}

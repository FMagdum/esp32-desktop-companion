#pragma once
#include <Arduino.h>
#include <FS.h>
#include "../config/SoundConfig.h"

namespace Wav {
struct Info {
  uint32_t dataBytes = 0;
  uint16_t channels = 0;
};
// On success file is positioned at PCM data; supports chunk padding/metadata.
bool parse(fs::File &file, Info &info, uint32_t maxSeconds = SoundConfig::MAX_SECONDS);
}

#pragma once
#include <Arduino.h>
namespace MusicArtwork {
constexpr int SIDE = 140;
struct Snapshot {
  char videoId[64] = {};
  bool ready = false, loading = false;
  uint16_t accent = 0xFDB0;
  uint32_t revision = 0;
};
bool begin();
// Called by the music network task. Copies its inputs and deduplicates requests.
void request(const char *videoId, const char *url);
Snapshot snapshot();
// Main-task copy. Never retains an unlocked pointer into the worker's image.
bool copyPixels(const char *videoId, uint32_t revision, uint16_t *destination);
}

#pragma once
#include <Arduino.h>

namespace Music {
enum class Command : uint8_t { Previous, PlayPause, Next, Play, Pause };
struct Snapshot {
  char status[48] = "Music not configured";
  char title[100] = "";
  char artist[100] = "";
  char videoId[64] = "";
  int trackState = -1;
  bool online = false;
  bool adPlaying = false, progressAvailable = false;
  uint32_t positionMs = 0, durationMs = 0, sampledAt = 0;
  uint32_t revision = 0;
};
// Extrapolate between the server's rate-limited polls, pause when offline or
// paused, and never advance beyond the duration. Unsigned subtraction tolerates
// the millis() wraparound. Cap extrapolation if an update is lost for too long.
inline uint32_t positionMsAt(const Snapshot &state, uint32_t now) {
  if (!state.progressAvailable || !state.durationMs) return 0;
  uint32_t value = state.positionMs < state.durationMs ? state.positionMs : state.durationMs;
  if (state.online && state.trackState == 1 && !state.adPlaying) {
    uint32_t delta = uint32_t(now - state.sampledAt);
    if (delta > 15000) delta = 15000;
    const uint32_t remaining = state.durationMs - value;
    value += delta < remaining ? delta : remaining;
  }
  return value;
}
// Load /companion.json before Sound::begin starts the SD playback task.
void begin(bool storageAvailable);
Snapshot snapshot();
bool command(Command command);
bool playVideo(const char *videoId);
bool phraseCommand(const char *words, Command &result);
}

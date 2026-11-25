#pragma once
#include <stddef.h>
#include <stdint.h>

namespace SpeechTiming {
constexpr uint32_t sampleRate = 16000;
constexpr uint32_t silenceFrames(uint32_t spokenFrames) {
  return spokenFrames <= sampleRate ? 9600 :
         spokenFrames <= 6 * sampleRate ? 16000 : 25600;
}

// The wake detector can fire while the wake phrase is still audible.
constexpr bool canEndTurn(uint32_t frames, uint32_t firstSpeech, uint32_t lastSpeech,
                          uint32_t wakeFrame, bool woke) {
  if (woke && frames - wakeFrame < sampleRate) return false;
  // A match can arrive just before the last syllable. Give residual audio in
  // the first 350 ms a full second of quiet before treating it as a turn.
  const bool couldBeWakeTail = woke && lastSpeech - wakeFrame <= sampleRate * 350 / 1000;
  const uint32_t quietNeeded = couldBeWakeTail ? sampleRate : silenceFrames(lastSpeech - firstSpeech);
  return frames - lastSpeech >= quietNeeded;
}

// Score the microphone channels independently. Reject bass-heavy bumps and
// impulsive frames before applying the sustained-activity requirement.
struct Frame {
  float level;
  bool shaped;
};
inline Frame measure(const int16_t *stereo, size_t frames) {
  if (!frames) return {};
  uint32_t best = 0;
  bool shaped = false;
  for (size_t channel = 0; channel < 2; ++channel) {
    uint32_t level = 0, movement = 0, peak = 0;
    int previous = stereo[channel];
    for (size_t i = 0; i < frames; ++i) {
      const int sample = stereo[2 * i + channel];
      const uint32_t magnitude = sample < 0 ? -sample : sample;
      const int delta = sample - previous;
      movement += delta < 0 ? -delta : delta;
      level += magnitude;
      if (magnitude > peak) peak = magnitude;
      previous = sample;
    }
    if (level > best) {
      best = level;
      shaped = movement * 16 >= level && movement * 2 <= level * 3 && peak * frames < level * 12;
    }
  }
  return {float(best) / frames, shaped};
}
}

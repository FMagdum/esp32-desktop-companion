#pragma once
#include <stdint.h>

// Speaker-dependent keyword spotting. One 16 ms frame at 16 kHz at a time.
// The reference stores log-band shapes, never raw microphone samples.
class WakePhrase {
public:
  static constexpr int Samples = 256, Bands = 12, MaxFrames = 96;
  struct Frame { int8_t band[Bands]; };
  struct Profile { uint8_t count; Frame frames[MaxFrames]; };
  enum class Event { None, Match, Learned, Retry };
  void reset();
  Event feed(const int16_t *mono, bool teaching);
  bool valid() const { return profile.count >= 15 && profile.count <= MaxFrames; }
  unsigned matchedSamples() const { return matchLength * Samples; }
  Profile profile{};
private:
  Frame history[MaxFrames]{};
  unsigned length = 0, end = 0, silence = 0, age = 0;
  bool active = false;
  float floor = 45;
  Frame spectrum(const int16_t *pcm);
  unsigned matchLength = 0;
  unsigned matchingFrames() const;
};


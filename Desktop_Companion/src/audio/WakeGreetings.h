#pragma once
#include "Sound.h"

class WakeGreetingAudio {
public:
  void start(Sound::WakeGreeting greeting);
  void stop() { remaining = 0; }
  bool active() const { return remaining != 0; }
  void render(int16_t *stereo, unsigned frames);
private:
  const int8_t *samples = nullptr;
  unsigned remaining = 0;
};

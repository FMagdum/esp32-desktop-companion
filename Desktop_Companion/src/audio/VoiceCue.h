#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

class VoiceCue {
public:
  enum Kind { Wake, Processing };
  void start(Kind kind) {
    frequency=kind==Wake ? 1000 : 660;
    total=kind==Wake ? 1024 : 1536; // 64 and 96 ms at 16 kHz
    position=0;
  }
  bool active() const { return position<total; }
  void render(int16_t *stereo, unsigned frames) {
    for (unsigned i=0;i<frames && active();++i,++position) {
      const float envelope=std::min(1.f,std::min(position,total-1-position)/80.f);
      const int16_t sample=int16_t(3000*envelope*std::sin(6.2831853f*frequency*position/16000.f));
      stereo[i*2]=stereo[i*2+1]=sample;
    }
  }
private:
  unsigned frequency=0, total=0, position=0;
};

// Remove the 1 kHz wake tone and its short acoustic tail from the capture,
// keeping the microphone live for words spoken immediately after "Sam".
class WakeToneNotch {
public:
  void filter(int16_t *stereo, unsigned frames) {
    for (unsigned i=0;i<frames*2;++i) {
      auto &s=channel[i%2];
      const float x=stereo[i];
      const float y=x-1.847759f*s.x1+s.x2+1.810804f*s.y1-0.9604f*s.y2;
      s.x2=s.x1; s.x1=x; s.y2=s.y1; s.y1=y;
      stereo[i]=int16_t(std::max(-32768.f,std::min(32767.f,y)));
    }
  }
private:
  struct State { float x1=0,x2=0,y1=0,y2=0; } channel[2]{};
};


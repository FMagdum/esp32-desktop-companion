#include "WakePhrase.h"
#include <math.h>
#include <stdlib.h>
#include <algorithm>

void WakePhrase::reset() {
  length = end = silence = age = 0;
  active = false;
}

// Log energies from a Hann-windowed FFT. Centering the bands within each
// frame makes the comparison less sensitive to microphone gain.
WakePhrase::Frame WakePhrase::spectrum(const int16_t *pcm) {
  struct Tables {
    float window[Samples], cs[Samples/2], sn[Samples/2];
    uint8_t reversed[Samples];
    Tables() {
      for (int i=0; i<Samples; ++i) {
        const float angle = 6.283185307f*i/Samples;
        window[i] = 0.5f-0.5f*cosf(angle);
        if (i<Samples/2) { cs[i]=cosf(angle); sn[i]=-sinf(angle); }
        unsigned r=0;
        for (int b=0; b<8; ++b) r=(r<<1)|((i>>b)&1);
        reversed[i]=r;
      }
    }
  };
  static const Tables tables;
  float real[Samples], imag[Samples]{};
  for (int i=0;i<Samples;++i) real[tables.reversed[i]]=pcm[i]*tables.window[i];
  for (int span=2;span<=Samples;span*=2) {
    for (int base=0;base<Samples;base+=span) for (int j=0;j<span/2;++j) {
      const int a=base+j, b=a+span/2, k=j*Samples/span;
      const float x=tables.cs[k]*real[b]-tables.sn[k]*imag[b];
      const float y=tables.sn[k]*real[b]+tables.cs[k]*imag[b];
      real[b]=real[a]-x; imag[b]=imag[a]-y; real[a]+=x; imag[a]+=y;
    }
  }
  constexpr int edges[Bands+1]={3,5,8,11,15,20,26,34,43,55,70,89,112};
  float logPower[Bands], mean=0;
  for (int b=0;b<Bands;++b) {
    float power=0;
    for (int k=edges[b];k<edges[b+1];++k) power+=real[k]*real[k]+imag[k]*imag[k];
    logPower[b]=logf(1.0f+power/(edges[b+1]-edges[b]));
    mean+=logPower[b]/Bands;
  }
  Frame f{};
  for (int b=0;b<Bands;++b)
    f.band[b]=std::max(-100,std::min(100,int((logPower[b]-mean)*6)));
  return f;
}

unsigned WakePhrase::matchingFrames() const {
  if (!valid()) return 0;
  for (int percent=80;percent<=120;percent+=10) {
    const unsigned n=profile.count*percent/100;
    if (n>MaxFrames || length<n) continue;
    unsigned distance=0, endingDistance=0;
    for (unsigned i=0;i<profile.count;++i) {
      const Frame &candidate=history[(end+MaxFrames-n+i*n/profile.count)%MaxFrames];
      for (int b=0;b<Bands;++b) {
        const unsigned delta=abs(int(candidate.band[b])-profile.frames[i].band[b]);
        distance+=delta;
        if (i+6>=profile.count) endingDistance+=delta;
      }
    }
    // Requiring the end to match suppresses similar prefixes ("Hey Sandy").
    if (distance < unsigned(profile.count)*Bands*6 && endingDistance < 6*Bands*8) return n;
  }
  return 0;
}

WakePhrase::Event WakePhrase::feed(const int16_t *mono, bool teaching) {
  float power=0;
  for (int i=0;i<Samples;++i) power+=float(mono[i])*mono[i];
  const float rms=sqrtf(power/Samples);
  const bool voiced=rms>std::max(75.0f, floor*2.8f);
  if (!active && !voiced) floor=0.98f*floor+0.02f*rms;
  ++age;
  if (teaching && age>375) { reset(); return Event::Retry; }
  if (!active && !voiced) return Event::None;
  active=true;
  silence=voiced ? 0 : silence+1;
  history[end]=spectrum(mono);
  end=(end+1)%MaxFrames;
  ++length;
  if (teaching && length>MaxFrames) { reset(); return Event::Retry; }
  if (!teaching && voiced && (matchLength = matchingFrames())) { reset(); return Event::Match; }
  if (silence<16) return Event::None;
  if (teaching && length-silence>=15) {
    const unsigned frames=length-silence;
    unsigned variation=0;
    for (unsigned i=4;i<frames;++i) for (int b=0;b<Bands;++b)
      variation+=abs(int(history[i].band[b])-history[i-4].band[b]);
    if (variation < (frames-4)*Bands*3) { reset(); return Event::Retry; }
    profile.count=frames;
    for (unsigned i=0;i<profile.count;++i) profile.frames[i]=history[i];
    reset(); return Event::Learned;
  }
  reset();
  return teaching ? Event::Retry : Event::None;
}


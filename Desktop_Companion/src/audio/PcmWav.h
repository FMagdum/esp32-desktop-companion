#pragma once
#include <stdint.h>
#include <string.h>

// Portable PCM16 RIFF header, shared by recorder and host-side format tests.
namespace PcmWav {
inline void put16(uint8_t *p, uint16_t v) { p[0]=uint8_t(v); p[1]=uint8_t(v >> 8); }
inline void put32(uint8_t *p, uint32_t v) {
  p[0]=uint8_t(v); p[1]=uint8_t(v >> 8); p[2]=uint8_t(v >> 16); p[3]=uint8_t(v >> 24);
}
inline void header(uint8_t *h, uint32_t dataBytes, uint32_t rate, uint16_t channels) {
  memset(h, 0, 44);
  memcpy(h,"RIFF",4); put32(h+4,36+dataBytes); memcpy(h+8,"WAVEfmt ",8);
  put32(h+16,16); put16(h+20,1); put16(h+22,channels); put32(h+24,rate);
  put32(h+28,rate*channels*2); put16(h+32,channels*2); put16(h+34,16);
  memcpy(h+36,"data",4); put32(h+40,dataBytes);
}
}

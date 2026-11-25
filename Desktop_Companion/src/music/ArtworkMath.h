#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ArtworkMath {
constexpr int SIDE = 140;
constexpr uint16_t FALLBACK = 0xFDB0;

// A bounded baseline-JPEG header walk, before handing bytes to the SDK decoder.
// Do not use the older SDK get-image-info parser on truncated network data.
inline bool dimensions(const uint8_t *data, size_t bytes, uint16_t &width, uint16_t &height) {
  width = height = 0;
  if (!data || bytes < 16 || data[0] != 0xFF || data[1] != 0xD8 ||
      data[bytes-2] != 0xFF || data[bytes-1] != 0xD9) return false;
  size_t offset = 2;
  bool frame = false;
  while (offset < bytes) {
    if (data[offset++] != 0xFF) return false;
    while (offset < bytes && data[offset] == 0xFF) ++offset;
    if (offset >= bytes) return false;
    const uint8_t marker = data[offset++];
    if (!marker || marker == 0xD8 || marker == 0xD9) return false;
    if (marker == 1 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (bytes - offset < 2) return false;
    const size_t length = size_t(data[offset]) * 256 + data[offset+1];
    if (length < 2 || length > bytes - offset) return false;
    const bool sof = marker >= 0xC0 && marker <= 0xCF &&
                     marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    if (sof) {
      if (frame || marker != 0xC0 || length < 11 || data[offset+2] != 8) return false;
      const uint8_t components = data[offset+7];
      if ((components != 1 && components != 3) || length != size_t(8 + 3*components)) return false;
      height = uint16_t(data[offset+3]) * 256 + data[offset+4];
      width = uint16_t(data[offset+5]) * 256 + data[offset+6];
      if (width < 8 || height < 8 || width > 1024 || height > 1024) return false;
      frame = true;
    }
    if (marker == 0xDA) return frame && length >= 6;
    offset += length;
  }
  return false;
}
// Thumbnail URLs never carry the desktop's Authorization header. Restrict
// requests to the public image hosts used by YouTube Music; no redirects.
inline bool allowedUrl(const char *url) {
  if (!url || strncmp(url, "https://", 8)) return false;
  const char *host = url + 8, *end = strchr(host, '/');
  if (!end || end == host || size_t(end-host) > 100) return false;
  for (const char *p = host; p != end; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return false;
  const char *domains[] = {"ytimg.com", "googleusercontent.com", "ggpht.com"};
  for (const char *domain : domains) {
    const size_t n = strlen(domain), h = end-host;
    if (h >= n && !memcmp(end-n, domain, n) && (h == n || *(end-n-1) == '.')) return true;
  }
  return false;
}
inline uint16_t rgb(int r, int g, int b) {
  return uint16_t((r & 248) << 8 | (g & 252) << 3 | b >> 3);
}
inline uint16_t accent(const uint16_t *pixels, size_t count) {
  struct Bin { uint32_t weight = 0, r = 0, g = 0, b = 0; } bins[12];
  for (size_t i = 0; i < count; ++i) {
    const uint16_t color = pixels[i];
    const int r = (color >> 11) * 255 / 31;
    const int g = ((color >> 5) & 63) * 255 / 63;
    const int b = (color & 31) * 255 / 31;
    const int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const int delta = hi-lo;
    if (hi < 45 || delta < 35 || delta * 255 / hi < 60) continue;
    int hue = hi == r ? (g-b)*256/delta :
              hi == g ? 512+(b-r)*256/delta : 1024+(r-g)*256/delta;
    if (hue < 0) hue += 1536;
    auto &bin = bins[(hue/128)%12];
    bin.weight += delta; bin.r += r*delta; bin.g += g*delta; bin.b += b*delta;
  }
  const Bin *best = &bins[0];
  for (const auto &bin : bins) if (bin.weight > best->weight) best = &bin;
  if (!best->weight) return FALLBACK;
  int r = best->r/best->weight, g = best->g/best->weight, b = best->b/best->weight;
  // Preserve the hue while lifting very dark colors for the navy background.
  const int luminance = (r*54 + g*183 + b*19)/256;
  if (luminance < 105) {
    const int amount = (105-luminance)*255/(255-luminance);
    r += (255-r)*amount/255; g += (255-g)*amount/255; b += (255-b)*amount/255;
  }
  return rgb(r,g,b);
}
inline void square(const uint16_t *source, int width, int height, uint16_t *target) {
  const int side = width < height ? width : height;
  const int ox = (width-side)/2, oy = (height-side)/2;
  for (int y = 0; y < SIDE; ++y) for (int x = 0; x < SIDE; ++x)
    target[y*SIDE+x] = source[(oy+(2*y+1)*side/(2*SIDE))*width + ox+(2*x+1)*side/(2*SIDE)];
}
}

#include "Artwork.h"
#include "ArtworkMath.h"
#include "../assistant/Assistant.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <time.h>
#include <string.h>

namespace {
constexpr size_t IMAGE_BYTES = MusicArtwork::SIDE * MusicArtwork::SIDE * sizeof(uint16_t);
constexpr size_t MAX_DOWNLOAD = 128 * 1024;
struct Request { char videoId[64], url[768]; uint32_t generation; };
QueueHandle_t pending = nullptr;
SemaphoreHandle_t imageLock = nullptr;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
MusicArtwork::Snapshot shared;
uint32_t generation = 0;
uint32_t completedAt = 0;
uint16_t *pixels = nullptr;
char requestedId[64] = {}, requestedUrl[768] = {}; // Music task only.
struct Buffer {
  uint8_t *data;
  explicit Buffer(size_t bytes) {
    data = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) data = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }
  ~Buffer() { free(data); }
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
};
bool current(const Request &request) {
  portENTER_CRITICAL(&stateLock);
  const bool result = request.generation == generation;
  portEXIT_CRITICAL(&stateLock);
  return result;
}
bool pause(const Request &request, uint32_t duration) {
  const uint32_t started = millis();
  while (uint32_t(millis()-started) < duration) {
    if (!current(request)) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return current(request);
}
bool decode(uint8_t *data, size_t bytes, uint16_t *target) {
  uint16_t width, height;
  if (!ArtworkMath::dimensions(data, bytes, width, height)) return false;
  uint8_t scale = 0;
  while (((width >> scale) > 320 || (height >> scale) > 320) && scale < 3) ++scale;
  const uint16_t scaledWidth = width >> scale, scaledHeight = height >> scale;
  if (!scaledWidth || !scaledHeight) return false;
  const size_t size = size_t(scaledWidth)*scaledHeight*2;
  Buffer decoded(size);
  if (!decoded.data) return false;
  esp_jpeg_image_cfg_t config = {};
  config.indata = data; config.indata_size = bytes;
  config.outbuf = decoded.data; config.outbuf_size = size;
  config.out_format = JPEG_IMAGE_FORMAT_RGB565;
  config.out_scale = static_cast<esp_jpeg_image_scale_t>(scale);
  config.flags.swap_color_bytes = 0; // Native little-endian uint16_t RGB565.
  esp_jpeg_image_output_t output = {};
  if (esp_jpeg_decode(&config, &output) != ESP_OK ||
      output.width != scaledWidth || output.height != scaledHeight ||
      output.output_len != size) return false;
  ArtworkMath::square(reinterpret_cast<uint16_t *>(decoded.data), scaledWidth, scaledHeight, target);
  return true;
}
bool fallbackUrl(const char *videoId, char (&url)[64]) {
  if (strlen(videoId) != 11) return false;
  for (const char *p = videoId; *p; ++p) {
    const char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/mqdefault.jpg", videoId);
  return true;
}

bool readArtwork(const Request &request, HTTPClient &http, int code, const char *source, uint16_t *target) {
  const int length = http.getSize();
  if (code != 200 || length == 0 || length > int(MAX_DOWNLOAD) || !current(request)) {
    if (current(request)) Serial.printf("Music artwork %s: HTTP %d, size %d\n", source, code, length);
    http.end(); return false;
  }
  const size_t capacity = length > 0 ? size_t(length) : MAX_DOWNLOAD;
  Buffer input(capacity);
  if (!input.data) { Serial.println("Music artwork: download buffer unavailable"); http.end(); return false; }
  auto *stream = http.getStreamPtr();
  size_t received = 0;
  const uint32_t started = millis();
  uint32_t lastData = started;
  bool valid = true;
  while (length < 0 || received < size_t(length)) {
    if (!current(request) || uint32_t(millis()-started) >= 8000 ||
        uint32_t(millis()-lastData) >= 2000) { valid = false; break; }
    const int available = stream->available();
    if (available > 0) {
      if (received == capacity) { valid = false; break; }
      const size_t count = min(min(size_t(available), capacity-received), size_t(2048));
      const int got = stream->read(input.data+received, count);
      if (got <= 0) { valid = false; break; }
      received += got; lastData = millis();
      if (length < 0 && received >= 2 &&
          input.data[received-2] == 0xFF && input.data[received-1] == 0xD9) break;
    } else if (!http.connected()) break;
    else vTaskDelay(pdMS_TO_TICKS(10));
  }
  http.end();
  if (!current(request)) return false;
  if (!valid || (length >= 0 && received != size_t(length))) {
    Serial.printf("Music artwork %s: incomplete download (%u bytes)\n", source, unsigned(received));
    return false;
  }
  const bool decoded = decode(input.data, received, target);
  if (!decoded) Serial.printf("Music artwork %s: unsupported JPEG (%u bytes)\n", source, unsigned(received));
  else Serial.printf("Music artwork %s: decoded\n", source);
  return decoded;
}
bool fetch(const Request &request, const char *url, uint16_t *target) {
  NetworkClientSecure socket;
  socket.useBuiltinCACertBundle();
  socket.setHandshakeTimeout(4);
  HTTPClient http;
  http.setConnectTimeout(2000); http.setTimeout(2000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  if (!http.begin(socket, url)) return false;
  http.addHeader("Accept", "image/jpeg");
  http.addHeader("Accept-Encoding", "identity");
  // This is a public thumbnail request: never attach the YTMDesktop token.
  return readArtwork(request, http, http.GET(), "direct", target);
}

bool fetchFromBridge(const Request &request, uint16_t *target) {
  NetworkClient socket;
  HTTPClient http;
  const int code = CompanionAssistant::requestArtwork(http, socket, request.videoId, request.url);
  return readArtwork(request, http, code, "bridge", target);
}

void finish(const Request &request, const uint16_t *image, bool success) {
  if (!current(request)) return;
  if (xSemaphoreTake(imageLock, portMAX_DELAY) != pdTRUE) return;
  if (success && current(request)) memcpy(pixels, image, IMAGE_BYTES);
  const uint16_t color = success ? ArtworkMath::accent(image, MusicArtwork::SIDE*MusicArtwork::SIDE) : ArtworkMath::FALLBACK;
  portENTER_CRITICAL(&stateLock);
  if (request.generation == generation) {
    shared.ready = success; shared.loading = false; shared.accent = color;
    completedAt = millis();
    ++shared.revision;
  }
  portEXIT_CRITICAL(&stateLock);
  xSemaphoreGive(imageLock);
  if (current(request)) Serial.println(success ? "Music artwork ready" : "Music artwork unavailable; using icon");
}
void worker(void *) {
  Request request;
  for (;;) {
    if (xQueueReceive(pending, &request, portMAX_DELAY) != pdTRUE || !current(request)) continue;
    char fallback[64]{};
    const bool hasFallback = fallbackUrl(request.videoId, fallback);
    if (!request.videoId[0] || (!request.url[0] && !hasFallback)) {
      finish(request, nullptr, false); continue;
    }
    Buffer frame(IMAGE_BYTES);
    if (!frame.data) { Serial.println("Music artwork: frame buffer unavailable"); finish(request, nullptr, false); continue; }
    const bool useFallback = hasFallback && strcmp(request.url, fallback);
    auto *target = reinterpret_cast<uint16_t *>(frame.data);
    bool success = false;
    for (int attempt = 0; attempt < 2 && current(request); ++attempt) {
      // Direct HTTPS needs a synced clock. The existing LAN bridge does not.
      if (time(nullptr) >= 1704067200 && request.url[0])
        success = fetch(request, request.url, target);
      if (!success && current(request) && hasFallback)
        success = fetchFromBridge(request, target);
      if (!success && useFallback && current(request) && time(nullptr) >= 1704067200)
        success = fetch(request, fallback, target);
      if (success || attempt == 1 || !pause(request, 5000)) break;
    }
    finish(request, reinterpret_cast<uint16_t *>(frame.data), success);
  }
}
}
namespace MusicArtwork {
Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  Snapshot result = shared;
  portEXIT_CRITICAL(&stateLock);
  return result;
}
bool begin() {
  if (pending) return true;
  pixels = static_cast<uint16_t *>(heap_caps_malloc(IMAGE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) pixels = static_cast<uint16_t *>(heap_caps_malloc(IMAGE_BYTES, MALLOC_CAP_8BIT));
  imageLock = xSemaphoreCreateMutex();
  pending = xQueueCreate(1, sizeof(Request));
  if (!pixels || !imageLock || !pending ||
      xTaskCreate(worker, "music-artwork", 12288, nullptr, 1, nullptr) != pdPASS) {
    if (pending) vQueueDelete(pending);
    if (imageLock) vSemaphoreDelete(imageLock);
    free(pixels); pending = nullptr; imageLock = nullptr; pixels = nullptr;
    Serial.println("Music artwork task unavailable; music controls continue");
    return false;
  }
  return true;
}
void request(const char *videoId, const char *url) {
  if (!pending || !videoId || !url || strlen(videoId) >= sizeof(requestedId)) return;
  if (!ArtworkMath::allowedUrl(url) || strlen(url) >= sizeof(requestedUrl)) url = "";
  char fallback[64];
  const bool canLoad = videoId[0] && (url[0] || fallbackUrl(videoId, fallback));
  if (!strcmp(requestedId, videoId) && !strcmp(requestedUrl, url)) {
    if (!canLoad) return;
    portENTER_CRITICAL(&stateLock);
    const bool retry = !shared.ready && !shared.loading && uint32_t(millis() - completedAt) >= 30000;
    portEXIT_CRITICAL(&stateLock);
    if (!retry) return;
  }
  strlcpy(requestedId, videoId, sizeof(requestedId));
  strlcpy(requestedUrl, url, sizeof(requestedUrl));
  Request value = {};
  strlcpy(value.videoId, videoId, sizeof(value.videoId));
  strlcpy(value.url, url, sizeof(value.url));
  portENTER_CRITICAL(&stateLock);
  value.generation = ++generation;
  strlcpy(shared.videoId, videoId, sizeof(shared.videoId));
  shared.ready = false; shared.loading = canLoad;
  shared.accent = ArtworkMath::FALLBACK; ++shared.revision;
  portEXIT_CRITICAL(&stateLock);
  xQueueOverwrite(pending, &value);
}
bool copyPixels(const char *videoId, uint32_t revision, uint16_t *destination) {
  if (!destination || !imageLock || xSemaphoreTake(imageLock, 0) != pdTRUE) return false;
  const auto state = snapshot();
  const bool valid = state.ready && state.revision == revision && !strcmp(state.videoId, videoId);
  if (valid) memcpy(destination, pixels, IMAGE_BYTES);
  xSemaphoreGive(imageLock);
  return valid;
}
}

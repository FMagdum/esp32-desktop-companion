#include "Music.h"
#include "Artwork.h"
#include "MusicMetadata.h"
#include "../network/Network.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace {
// YTMDesktop 2.1.0: randomBytes(256).toString("hex") = 512 characters.
char ssid[33] = {}, password[65] = {}, token[513] = {};
IPAddress host;
uint16_t port = 9863;
struct Request { Music::Command command; char videoId[12]; };
QueueHandle_t commands = nullptr;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Music::Snapshot state;
bool taskStarted = false;

void publish(Music::Snapshot next) {
  portENTER_CRITICAL(&stateLock);
  next.revision = state.revision + 1;
  state = next;
  portEXIT_CRITICAL(&stateLock);
}
void status(const char *message) {
  auto next = Music::snapshot();
  if (!strcmp(message, next.status) && !next.online) return;
  strlcpy(next.status, message, sizeof(next.status));
  next.online = false;
  next.title[0] = next.artist[0] = next.videoId[0] = 0;
  MusicArtwork::request("", "");
  next.trackState = -1;
  next.progressAvailable = false; next.adPlaying = false;
  next.positionMs = next.durationMs = 0;
  publish(next);
  Serial.printf("Music: %s\n", message);
}
bool copyField(JsonVariantConst value, char *dest, size_t capacity, bool allowEmpty = false) {
  if (!value.is<const char *>()) return false;
  const char *text = value.as<const char *>();
  size_t length = strlen(text);
  if ((!allowEmpty && !length) || length >= capacity) return false;
  memcpy(dest, text, length + 1);
  return true;
}
bool loadConfig() {
  auto file = SD_MMC.open("/companion.json", FILE_READ);
  if (!file || file.isDirectory() || file.size() > 2048) return false;
  JsonDocument doc;
  auto error = deserializeJson(doc, file);
  file.close();
  if (error) return false;
  char hostText[16] = {};
  if (!copyField(doc["ssid"], ssid, sizeof(ssid)) ||
      !copyField(doc["password"], password, sizeof(password), true) ||
      !copyField(doc["host"], hostText, sizeof(hostText)) ||
      !host.fromString(hostText) || host == IPAddress(0,0,0,0) ||
      host[0] == 127 || host[0] >= 224 ||
      !copyField(doc["token"], token, sizeof(token))) return false;
  // Headers must contain printable characters only, without CR/LF.
  for (const char *p = token; *p; ++p) if (*p < 33 || *p > 126) return false;
  if (!doc["port"].isNull()) {
    if (!doc["port"].is<unsigned int>()) return false;
    unsigned int value = doc["port"].as<unsigned int>();
    if (!value || value > 65535) return false;
    port = value;
  }
  return true;
}

// Keep timeouts/parsing bounded; queue fields are discarded.
int request(const char *path, const char *body, JsonDocument *result, uint32_t &backoff) {
  WiFiClient socket;
  HTTPClient http;
  String url = String("http://") + host.toString() + ":" + port + path;
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  http.useHTTP10(true); // Non-chunked stream for ArduinoJson.
  if (!http.begin(socket, url)) return -1;
  const char *headers[] = {"Retry-After"};
  http.collectHeaders(headers, 1);
  http.addHeader("Authorization", token); // Raw token, not Bearer.
  http.addHeader("Content-Type", "application/json");
  const int code = body ? http.POST(String(body)) : http.GET();
  if (code == 429) {
    int seconds = http.header("Retry-After").toInt();
    backoff = uint32_t(constrain(seconds > 0 ? seconds : 60, 1, 3600)) * 1000;
  }
  if (code == 200 && result) {
    JsonDocument filter;
    MusicMetadata::filter(filter);
    const int size = http.getSize();
    if (size > 262144) { http.end(); return -2; }
    auto error = deserializeJson(*result, http.getStream(),
                                 DeserializationOption::Filter(filter),
                                 DeserializationOption::NestingLimit(20));
    if (error) { http.end(); return -2; }
  }
  http.end();
  return code;
}

void worker(void *) {
  CompanionNetwork::begin(ssid, password);
  // YTMDesktop 2.1.0: GET /state once per 5s; POST /command twice per second.
  uint32_t lastStatePoll = millis() - 5500;
  uint32_t lastCommand = millis() - 600;
  uint32_t backoffStart = 0, backoffMs = 0;
  bool authFailed = false;
  for (;;) {
    const uint32_t now = millis();
    // Wi-Fi reconnects are coordinated on the main loop.
    if (!CompanionNetwork::connected()) {
      status("Connecting Wi-Fi");
      Request discarded;
      while (xQueueReceive(commands, &discarded, 0) == pdTRUE) {}
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (authFailed) {
      status("Pairing expired - re-pair");
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (backoffMs && uint32_t(now - backoffStart) < backoffMs) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    backoffMs = 0;
    Request cmd;
    bool attempted = false;
    uint32_t retryAfter = 0;
    int code = 0;
    if (uint32_t(now - lastCommand) >= 600 &&
        xQueueReceive(commands, &cmd, 0) == pdTRUE) {
      attempted = true;
      char body[96];
      if (cmd.videoId[0]) {
        snprintf(body,sizeof(body),R"({"command":"changeVideo","data":{"videoId":"%s"}})",cmd.videoId);
      } else {
        const char *names[]={"previous","playPause","next","play","pause"};
        snprintf(body,sizeof(body),R"({"command":"%s"})",names[unsigned(cmd.command)]);
      }
      code = request("/api/v1/command", body, nullptr, retryAfter);
      lastCommand = millis();
      // Do not replay a command after timeout: it may already have succeeded.
      if (code >= 200 && code < 300) {
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
      }
    } else if (uint32_t(now - lastStatePoll) >= 5500) {
      attempted = true;
      JsonDocument result;
      code = request("/api/v1/state", nullptr, &result, retryAfter);
      lastStatePoll = millis();
      if (code == 200 && result["player"]["trackState"].is<int>()) {
        Music::Snapshot next;
        next.online = true;
        next.trackState = result["player"]["trackState"].as<int>();
        strlcpy(next.status, "YTMDesktop connected", sizeof(next.status));
        strlcpy(next.title, result["video"]["title"] | "No track selected", sizeof(next.title));
        strlcpy(next.artist, result["video"]["author"] | "", sizeof(next.artist));

        const char *id = result["video"]["id"] | "";
        if (strlen(id) < sizeof(next.videoId)) strlcpy(next.videoId, id, sizeof(next.videoId));
        MusicMetadata::progress(result["player"], result["video"], next, millis());
        publish(next);
        MusicArtwork::request(next.videoId, MusicMetadata::thumbnail(result["video"]));
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
      }
      if (code == 200) code = -2;
    }
    if (attempted) {
      if (code == 401 || code == 403) {
        status("Pairing expired - re-pair");
        authFailed = true;
      } else if (code == 429) {
        status("Music rate limited");
      } else if (code == -2) {
        status("Unsupported music response");
      } else {
        status("Desktop unavailable");
      }
      Request discarded;
      while (xQueueReceive(commands, &discarded, 0) == pdTRUE) {}
      backoffStart = millis();
      backoffMs = retryAfter ? retryAfter : 5500;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}
}
namespace Music {
Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  Snapshot copy = state;
  portEXIT_CRITICAL(&stateLock);
  return copy;
}
void begin(bool storageAvailable) {
  if (taskStarted) return;
  if (!storageAvailable || !loadConfig()) {
    status("Setup /companion.json");
    return;
  }
  MusicArtwork::begin();
  commands = xQueueCreate(1, sizeof(Request));
  if (!commands) { status("Music queue unavailable"); return; }
  if (xTaskCreate(worker, "music-network", 10240, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(commands);
    commands = nullptr;
    status("Music task unavailable");
    return;
  }
  taskStarted = true;
}
bool command(Command cmd) {
  if (!commands || !snapshot().online) return false;
  Request request{cmd,{}};
  return xQueueSend(commands, &request, 0) == pdTRUE;
}
bool playVideo(const char *id) {
  if (!commands || !snapshot().online || !id || strlen(id)!=11 ||
      strspn(id,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=11) return false;
  Request request{};
  strlcpy(request.videoId,id,sizeof(request.videoId));
  return xQueueSend(commands,&request,0)==pdTRUE;
}
bool phraseCommand(const char *words, Command &result) {
  // Read-only firmware data persists in flash across power cycles.
  static constexpr struct { const char *phrase; Command action; } commandsByPhrase[]={
    {"pause",Command::Pause},{"pause music",Command::Pause},{"pause the music",Command::Pause},
    {"play",Command::Play},{"resume",Command::Play},{"resume music",Command::Play},
    {"next",Command::Next},{"next song",Command::Next},{"next track",Command::Next},
    {"skip",Command::Next},{"previous",Command::Previous},{"previous song",Command::Previous},
    {"previous track",Command::Previous}
  };
  String phrase(words);
  phrase.trim(); phrase.toLowerCase();
  while (phrase.length() && strchr(".!?,",phrase[phrase.length()-1])) phrase.remove(phrase.length()-1);
  if (phrase.startsWith("hey sam")) {
    phrase.remove(0,7);
    while (phrase.length() && strchr(",. ",phrase[0])) phrase.remove(0,1);
  }
  for (const auto &entry : commandsByPhrase) if (phrase==entry.phrase) {
    result=entry.action; return true;
  }
  return false;
}
}




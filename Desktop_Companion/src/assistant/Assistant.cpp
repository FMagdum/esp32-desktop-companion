#include "Assistant.h"
#include "Text.h"
#include "LocalIntents.h"
#include <atomic>
#include "../music/Music.h"
#include "../audio/Sound.h"
#include "../audio/Wav.h"
#include "../config/SoundConfig.h"
#include "../config/UIConfig.h"
#include "../network/Network.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {
IPAddress host;
uint16_t port = 9870;
char bridgeToken[65] = {};
char sessionId[33] = {};
enum Request : uint8_t { Question, MusicQuestion, PromptAnswer, FollowUp };
std::atomic<bool> conversation{false}, homeCanceled{false};
std::atomic<uint32_t> conversationId{0};
uint32_t lastConversationEnded = 0;
std::atomic<int> requestedVolume{-1};
std::atomic<int> volumeResult{0};
QueueHandle_t questions = nullptr;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
CompanionAssistant::Snapshot shared;
RemoteSettings::Status deviceStatus;
RemoteSettings::Command remoteCommand;
bool commandWaiting=false;
uint32_t deliveredId=0;

void publish(const CompanionAssistant::Snapshot &value) {
  portENTER_CRITICAL(&stateLock);
  const uint32_t revision = shared.revision + 1;
  shared = value;
  shared.revision = revision;
  portEXIT_CRITICAL(&stateLock);
}
void updateStatus(const char *message, bool busy) {
  portENTER_CRITICAL(&stateLock);
  shared.visible = true;
  shared.busy = busy;
  strlcpy(shared.status, message, sizeof(shared.status));
  ++shared.revision;
  portEXIT_CRITICAL(&stateLock);
  Serial.printf("Assistant: %s\n", message);
}
bool loadConfig() {
  auto file = SD_MMC.open("/assistant.json", FILE_READ);
  if (!file || file.isDirectory() || file.size() > 1024) return false;
  JsonDocument doc;
  auto error = deserializeJson(doc, file);
  file.close();
  if (error || !doc["host"].is<const char *>() || !doc["token"].is<const char *>()) return false;
  const char *ip = doc["host"];
  const char *token = doc["token"];
  if (!host.fromString(ip) || host[0] == 127 || host[0] == 0 || host[0] >= 224 ||
      strlen(token) != 64) return false;
  for (const char *p = token; *p; ++p)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  strlcpy(bridgeToken, token, sizeof(bridgeToken));
  if (!doc["port"].isNull()) {
    if (!doc["port"].is<unsigned int>()) return false;
    const unsigned int configuredPort = doc["port"];
    if (!configuredPort || configuredPort > 65535) return false;
    port = configuredPort;
  }
  return true;
}
bool startHttp(HTTPClient &http, WiFiClient &socket, const String &path) {
  http.setConnectTimeout(1500);
  http.setTimeout(5000);
  http.useHTTP10(true);
  if (!http.begin(socket, String("http://") + host.toString() + ":" + port + path)) return false;
  http.addHeader("Authorization", String("Bearer ") + bridgeToken);
  return true;
}
bool readJson(HTTPClient &http, JsonDocument &doc) {
  const int length = http.getSize();
  if (length < 1 || length > 8192) return false;
  auto error = deserializeJson(doc, http.getStream(), DeserializationOption::NestingLimit(8));
  return !error;
}
void syncDevice() {
  if (!CompanionNetwork::connected()) return;
  RemoteSettings::Status value;
  portENTER_CRITICAL(&stateLock);
  value=deviceStatus;
  portEXIT_CRITICAL(&stateLock);
  JsonDocument doc;
  doc["session"]=sessionId; doc["conversation"]=conversation.load(); doc["ack"]=value.ack; doc["result"]=value.result;
  auto status=doc["status"].to<JsonObject>();
  const auto &v=value.values;
  status["wakeEachTurn"]=v.wakeEachTurn; status["followUpPrompt"]=v.followUpPrompt;
  status["brightness"]=v.brightness; status["volume"]=v.volume;
  status["microphoneDb"]=v.microphoneDb; status["recordSeconds"]=v.recordSeconds;
  status["palette"]=v.palette; status["lightMode"]=v.lightMode;
  status["latitude"]=v.latitude; status["longitude"]=v.longitude;
  status["location"]=v.location; status["wakePhrase"]=v.wakePhrase;
  status["firmware"]=UIConfig::FIRMWARE; status["flashMb"]=value.flashMb; status["psramMb"]=value.psramMb;
  status["musicOnline"]=value.musicOnline; status["wakeTrained"]=value.wakeTrained; status["teaching"]=value.teaching;
  status["wakeTest"]=value.wakeTest;
  status["sdAvailable"]=value.sdAvailable; status["sdTotal"]=value.sdTotal; status["sdUsed"]=value.sdUsed;
  String body; serializeJson(doc,body); doc.clear();
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http,socket,"/device")) return;
  http.addHeader("Content-Type","application/json");
  const bool received=http.POST(body)==200 && readJson(http,doc);
  http.end();
  if (!received) return;
  const auto request=doc["command"];
  const uint32_t id=request["id"] | uint32_t(0);
  if (!id || id==deliveredId) return;
  RemoteSettings::Command command;
  command.id=id;
  const char *action=request["action"] | "";
  if (!strcmp(action,"enroll")) command.action=RemoteSettings::Command::Enroll;
  else if (!strcmp(action,"test")) command.action=RemoteSettings::Command::Test;
  else if (!strcmp(action,"sleep")) command.action=RemoteSettings::Command::Sleep;
  else if (!strcmp(action,"settings")) {
    const auto settings=request["settings"];
    auto &v=command.values;
    if (!settings["brightness"].is<int>() || !settings["volume"].is<int>() ||
        !settings["microphoneDb"].is<int>() || !settings["recordSeconds"].is<int>() ||
        !settings["palette"].is<int>() || !settings["lightMode"].is<bool>() ||
        !settings["wakePhrase"].is<const char*>()) return;
    const int brightness=settings["brightness"], volume=settings["volume"], mic=settings["microphoneDb"],
              seconds=settings["recordSeconds"], palette=settings["palette"];
    if (brightness<10 || brightness>100 || volume<0 || volume>100 || mic< -12 || mic>12 ||
        seconds<int(SoundConfig::MIN_SECONDS) || seconds>int(SoundConfig::MAX_SECONDS) || palette<0 || palette>6) return;
    v.brightness=brightness; v.volume=volume; v.microphoneDb=mic; v.recordSeconds=seconds; v.palette=palette;
    v.lightMode=settings["lightMode"];
    v.wakeEachTurn=settings["wakeEachTurn"] | value.values.wakeEachTurn;
    v.followUpPrompt=settings["followUpPrompt"] | value.values.followUpPrompt;
    v.latitude=value.values.latitude;
    v.longitude=value.values.longitude;
    strlcpy(v.location, value.values.location, sizeof(v.location));
    const char *phrase=settings["wakePhrase"];
    if (strlen(phrase)<2 || strlen(phrase)>28) return;
    strlcpy(v.wakePhrase,phrase,sizeof(v.wakePhrase));
    command.action=RemoteSettings::Command::Settings;
  } else return;
  portENTER_CRITICAL(&stateLock);
  if (!commandWaiting) { remoteCommand=command; commandWaiting=true; deliveredId=id; }
  portEXIT_CRITICAL(&stateLock);
}
void checkConnection() {
  auto connection = CompanionAssistant::Connection::Offline;
  char message[80];
  strlcpy(message, CompanionNetwork::connected() ? "Assistant bridge unavailable" : "Connect Wi-Fi first", sizeof(message));
  if (CompanionNetwork::connected()) {
    WiFiClient socket;
    HTTPClient http;
    if (startHttp(http, socket, "/health")) {
      const int code = http.GET();
      JsonDocument doc;
      if (code == 200 && readJson(http, doc) && doc["ready"] == true && doc["protocol"] == 1) {
        const char *error = doc["api_error"] | "";
        connection = error[0] ? CompanionAssistant::Connection::ApiError : CompanionAssistant::Connection::Ready;
        strlcpy(message, error[0] ? error : "Assistant bridge connected", sizeof(message));
      } else if (code == 401) {
        strlcpy(message, "Bridge token mismatch", sizeof(message));
      }
      http.end();
    }
  }
  portENTER_CRITICAL(&stateLock);
  shared.connection = connection;
  strlcpy(shared.connectionMessage, message, sizeof(shared.connectionMessage));
  ++shared.revision;
  portEXIT_CRITICAL(&stateLock);
}
bool validJobId(const char *id) {
  if (!id || strlen(id) != 32) return false;
  for (const char *p = id; *p; ++p)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  return true;
}
void setTurnHeaders(HTTPClient &http) {
  char turnId[33];
  memcpy(turnId, sessionId, 24);
  snprintf(turnId + 24, 9, "%08lx", (unsigned long)conversationId.load());
  http.addHeader("X-Companion-Session", turnId);
}

bool submitText(char *jobId, const char *transcript, const char *reply) {
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http, socket, reply ? "/say" : "/respond")) return false;
  setTurnHeaders(http);
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["transcript"] = transcript;
  if (reply) doc["reply"] = reply;
  String body;
  serializeJson(doc, body);
  doc.clear();
  const bool accepted = http.POST(body) == 202 && readJson(http, doc) && validJobId(doc["job_id"] | "");
  if (accepted) strlcpy(jobId, doc["job_id"], 33);
  http.end();
  return accepted;
}

LocalIntents::Reply interpretLocalRequest(const char *transcript, const LocalIntents::Reply &fallback) {
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http, socket, "/intent")) return fallback;
  http.setTimeout(11000);
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["transcript"] = transcript;
  String payload;
  serializeJson(doc, payload);
  doc.clear();
  const int code = http.POST(payload);
  const bool received = code == 200 && readJson(http, doc);
  http.end();
  if (!received || homeCanceled.load()) {
    Serial.printf("Assistant: intent bridge HTTP %d\n", code);
    return fallback;
  }
  using namespace LocalIntents;
  Intent intent;
  const char *kind = doc["kind"] | "";
  if (!strcmp(kind, "clock")) intent.kind = Interpretation::Clock;
  else if (!strcmp(kind, "alarm")) {
    intent.kind = Interpretation::Alarm;
    const char *day = doc["day"] | "";
    if (!strcmp(day, "today")) intent.day = AlarmDay::Today;
    else if (!strcmp(day, "tomorrow")) intent.day = AlarmDay::Tomorrow;
    else if (strcmp(day, "next")) return fallback;
    if ((!doc["seconds"].isNull() && !doc["seconds"].is<int>()) ||
        (!doc["hour"].isNull() && !doc["hour"].is<int>()) ||
        (!doc["minute"].isNull() && !doc["minute"].is<int>()) ||
        !doc["ambiguous"].is<bool>()) return fallback;
    intent.delay = doc["seconds"] | 0;
    intent.hour = doc["hour"] | -1;
    intent.minute = doc["minute"] | 0;
    intent.ambiguous = doc["ambiguous"].as<bool>();
  } else return fallback;
  const auto reply = apply(intent);
  return reply.action != Action::None ? reply : fallback;
}

bool upload(char *jobId, bool music) {
  auto file = SD_MMC.open(SoundConfig::RECORD_PATH, FILE_READ);
  Wav::Info info;
  if (!file || !Wav::parse(file, info) || file.size() > SoundConfig::SAMPLE_RATE * SoundConfig::MAX_SECONDS * 4 + 100 || !file.seek(0)) {
    if (file) file.close();
    updateStatus("Record a question first", false);
    return false;
  }
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http, socket, music ? "/music/voice" : "/transcribe")) { file.close(); return false; }
  http.addHeader("Content-Type", "audio/wav");
  setTurnHeaders(http);
  // Explicit action, exactly one POST. Never retry an uncertain paid request.
  Serial.printf("Voice upload: %u bytes\n", unsigned(file.size()));
  const int code = http.sendRequest("POST", &file, file.size());
  Serial.printf("Voice upload: HTTP %d\n", code);
  file.close();
  JsonDocument doc;
  const bool received = readJson(http, doc);
  http.end();
  if (code == 202 && received && validJobId(doc["job_id"] | "")) {
    strlcpy(jobId, doc["job_id"], 33);
    return true;
  }
  if (code == 401) updateStatus("Bridge token mismatch", false);
  else if (code == 409) updateStatus("Bridge busy - try later", false);
  else if (code == 400 && received) updateStatus(doc["error"] | "Invalid question audio", false);
  else updateStatus("Bridge unavailable; not retried", false);
  return false;
}
bool poll(const char *jobId, const char *localReply = nullptr) {
  const uint32_t started = millis();
  while (uint32_t(millis()-started) < 120000) {
    if (!CompanionNetwork::connected()) {
      updateStatus("Wi-Fi disconnected; not retried", false);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    syncDevice();
    if (homeCanceled.load()) return false;
    WiFiClient socket;
    HTTPClient http;
    if (!startHttp(http, socket, String("/jobs/") + jobId)) return false;
    const int code = http.GET();
    JsonDocument doc;
    const bool received = code == 200 && readJson(http, doc);
    http.end();
    if (homeCanceled.load()) return false;
    if (!received) {
      updateStatus("Lost bridge connection", false);
      return false;
    }
    auto value = CompanionAssistant::snapshot();
    if (!localReply) AssistantText::copyUtf8(value.transcript, sizeof(value.transcript), doc["transcript"] | "");
    AssistantText::copyUtf8(value.reply, sizeof(value.reply), localReply ? localReply : doc["reply"] | "");
    strlcpy(value.status, doc["stage"] | "Thinking", sizeof(value.status));
    publish(value);
    const char *status = doc["status"] | "";
    if (!strcmp(status, "complete")) {
      if (!value.music && !strcmp(doc["action"] | "", "music")) {
        value.musicNavigation++;
        publish(value);
      } else if (!value.music && !strcmp(doc["action"] | "", "chat")) {
        value.chatNavigation++;
        publish(value);
      }
      return true;
    }
    if (!strcmp(status, "error")) {
      updateStatus(doc["error"] | "Cloud request failed", false);
      return false;
    }
    if (strcmp(status, "pending")) {
      updateStatus("Invalid bridge response", false);
      return false;
    }
  }
  updateStatus("Cloud timeout; not retried", false);
  return false;
}
bool download(const char *jobId) {
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http, socket, String("/audio/") + jobId)) return false;
  const int code = http.GET();
  const int size = http.getSize();
  if (code != 200 || size < 44 || size > 960044 ||
      (!SD_MMC.exists("/assistant") && !SD_MMC.mkdir("/assistant"))) {
    http.end();
    return false;
  }
  auto file = SD_MMC.open(SoundConfig::ASSISTANT_PENDING_PATH, FILE_WRITE);
  const int written = file ? http.writeToStream(&file) : -1;
  if (file) { file.flush(); file.close(); }
  http.end();
  auto verify = SD_MMC.open(SoundConfig::ASSISTANT_PENDING_PATH, FILE_READ);
  Wav::Info info;
  const bool valid = written == size && verify && Wav::parse(verify, info, 30);
  if (verify) verify.close();
  if (!valid) { SD_MMC.remove(SoundConfig::ASSISTANT_PENDING_PATH); return false; }
  if (SD_MMC.exists(SoundConfig::ASSISTANT_REPLY_PATH) &&
      !SD_MMC.remove(SoundConfig::ASSISTANT_REPLY_PATH)) return false;
  return SD_MMC.rename(SoundConfig::ASSISTANT_PENDING_PATH, SoundConfig::ASSISTANT_REPLY_PATH);
}
bool dispatchMusic(const char *words) {
  Music::Command cmd;
  if (Music::phraseCommand(words,cmd)) return Music::command(cmd);
  WiFiClient socket;
  HTTPClient http;
  if (!startHttp(http,socket,"/music/search")) return false;
  http.setTimeout(15000);
  http.addHeader("Content-Type","application/json");
  JsonDocument doc; doc["query"]=words;
  String body; serializeJson(doc,body); doc.clear();
  const bool found=http.POST(body)==200 && readJson(http,doc);
  http.end();
  return found && Music::playVideo(doc["videoId"] | "");
}
bool requestsStandby(const char *text, bool answeringPrompt) {
  String phrase;
  for (auto p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    if (isalnum(*p)) phrase += char(tolower(*p));
    else if (phrase.length() && !phrase.endsWith(" ")) phrase += ' ';
  }
  phrase.trim();
  if (phrase == "stop listening" || phrase == "go to standby") return true;
  return answeringPrompt && (phrase == "no" || phrase == "no thanks" || phrase == "no thank you" ||
                             phrase == "that s all" || phrase == "thats all" || phrase == "that is all");
}

// Hold a recognized Home request before it reaches intent execution or ChatGPT.
bool reviewHomeTurn() {
  constexpr uint32_t reviewMs = 3000;
  const uint32_t started = millis();
  while (!homeCanceled.load() && uint32_t(millis() - started) < reviewMs)
    vTaskDelay(pdMS_TO_TICKS(20));
  return !homeCanceled.load();
}

void finishCanceledTurn() {
  LocalIntents::clearPendingAlarm();
  portENTER_CRITICAL(&stateLock);
  shared.busy = shared.replyReady = shared.visible = false;
  shared.transcript[0] = shared.reply[0] = 0;
  strlcpy(shared.status, "Standby", sizeof(shared.status));
  ++shared.revision;
  portEXIT_CRITICAL(&stateLock);
  Serial.println("Assistant: Home turn canceled");
  syncDevice();
}

void worker(void *) {
  uint8_t request;
  uint32_t lastHealth=millis()-10000;
  for (;;) {
    if (xQueueReceive(questions, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
      syncDevice();
      if (uint32_t(millis()-lastHealth)>=10000) { checkConnection(); lastHealth=millis(); }
      continue;
    }
    if (homeCanceled.load()) { finishCanceledTurn(); continue; }
    if (!CompanionNetwork::connected()) { updateStatus("Connect Wi-Fi first", false); continue; }
    syncDevice();
    if (!Sound::reserveForAssistant()) { updateStatus("Audio busy - try after playback", false); continue; }
    const auto before = CompanionAssistant::snapshot();
    const uint32_t musicBefore = before.musicNavigation, chatBefore = before.chatNavigation;
    char jobId[33] = {};
    const bool music = request == MusicQuestion;
    const bool captured = request == Question || request == PromptAnswer || music;
    bool completed = false;
    String clockReply;
    if (captured) {
      updateStatus("Sending question", true);
      if (upload(jobId, music)) {
        Sound::processingCue();
        updateStatus("Recognizing speech", true);
        completed = poll(jobId);
      }
    } else {
      updateStatus("Preparing Sam's voice", true);
      completed = submitText(jobId, "", "Do you need anything else?") && poll(jobId);
    }
    if (captured && completed && !homeCanceled.load()) {
      const auto recognized = CompanionAssistant::snapshot();
      if (!recognized.transcript[0] || requestsStandby(recognized.transcript, request == PromptAnswer)) {
        Sound::releaseAssistant();
        if (recognized.transcript[0]) LocalIntents::clearPendingAlarm();
        portENTER_CRITICAL(&stateLock);
        if (recognized.transcript[0]) ++shared.standbyRequests;
        shared.busy = false;
        shared.visible = false;
        strlcpy(shared.status, recognized.transcript[0] ? "Standby" : "No question heard", sizeof(shared.status));
        ++shared.revision;
        portEXIT_CRITICAL(&stateLock);
        continue;
      }
      if (before.homeTurn && !reviewHomeTurn()) {
        Sound::releaseAssistant();
        finishCanceledTurn();
        continue;
      }
      portENTER_CRITICAL(&stateLock);
      ++shared.acceptedTurns;
      portEXIT_CRITICAL(&stateLock);
    }
    if (music && completed) {
      Sound::releaseAssistant();
      const auto result = CompanionAssistant::snapshot();
      const bool sent = dispatchMusic(result.transcript);
      updateStatus(sent ? "Music command queued" : "Search or desktop unavailable", false);
      checkConnection();
      continue;
    }
    if (captured && completed && !homeCanceled.load()) {
      const auto transcript = CompanionAssistant::snapshot();
      int volume;
      portENTER_CRITICAL(&stateLock);
      volume = deviceStatus.values.volume;
      portEXIT_CRITICAL(&stateLock);
      auto reply = LocalIntents::match(transcript.transcript, volume, Music::snapshot().online);
      if (reply.action == LocalIntents::Action::Interpret) {
        const LocalIntents::Reply fallback{reply.text, reply.text.length() ? LocalIntents::Action::Clock : LocalIntents::Action::None};
        reply = interpretLocalRequest(transcript.transcript, fallback);
      }
      if (reply.action == LocalIntents::Action::Volume) {
        volumeResult.store(0);
        requestedVolume.store(reply.volume);
        const auto started = millis();
        while (!volumeResult.load() && uint32_t(millis() - started) < 1500) vTaskDelay(pdMS_TO_TICKS(20));
        requestedVolume.store(-1);
        if (volumeResult.load() != 1) reply.text = "I couldn't change the speaker volume.";
      } else if (reply.action == LocalIntents::Action::Music && !dispatchMusic(transcript.transcript)) {
        reply.text = "Desktop music is unavailable or the song wasn't found.";
      }
      if (reply.action == LocalIntents::Action::Clock) {
        clockReply = reply.text;
        portENTER_CRITICAL(&stateLock);
        strlcpy(shared.reply, clockReply.c_str(), sizeof(shared.reply));
        ++shared.revision;
        portEXIT_CRITICAL(&stateLock);
        Serial.println("Assistant: resolved clock or alarm on device");
      }
      updateStatus(reply.text.length() ? "Preparing local reply" : "Thinking", true);
      completed = !homeCanceled.load() && submitText(jobId, transcript.transcript, reply.text.length() ? reply.text.c_str() : nullptr)
                  && poll(jobId, clockReply.length() ? clockReply.c_str() : nullptr);
    }
    const auto result = CompanionAssistant::snapshot();
    const bool navigation = completed && (result.musicNavigation != musicBefore || result.chatNavigation != chatBefore);
    const bool downloaded = completed && !navigation && !homeCanceled.load() && download(jobId);
    Sound::releaseAssistant();
    if (homeCanceled.load()) { finishCanceledTurn(); continue; }
    if (navigation) {
      updateStatus(result.chatNavigation != chatBefore ? "Opened Chat" : "Opened Music", false);
    } else if (downloaded) {
      portENTER_CRITICAL(&stateLock);
      shared.replyReady = !homeCanceled.load();
      shared.busy = false;
      strlcpy(shared.status, request == FollowUp ? "Do you need anything else?" : "Reply ready",
              sizeof(shared.status));
      ++shared.revision;
      portEXIT_CRITICAL(&stateLock);
    } else if (clockReply.length()) {
      portENTER_CRITICAL(&stateLock);
      strlcpy(shared.reply, clockReply.c_str(), sizeof(shared.reply));
      shared.visible = true;
      shared.busy = false;
      shared.replyReady = false;
      strlcpy(shared.status, "Local reply", sizeof(shared.status));
      ++shared.revision;
      portEXIT_CRITICAL(&stateLock);
    } else if (completed) {
      updateStatus("Reply download failed", false);
    } else if (CompanionAssistant::snapshot().busy) {
      updateStatus("Assistant request failed", false);
    }
    checkConnection();
  }
}
}
namespace CompanionAssistant {
Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  Snapshot value = shared;
  portEXIT_CRITICAL(&stateLock);
  return value;
}
void begin(bool storageAvailable) {
  if (storageAvailable) LocalIntents::begin();
  if (!storageAvailable || !loadConfig()) {
    auto value = snapshot();
    value.connection = Connection::Offline;
    strlcpy(value.connectionMessage, "Configure assistant.json on SD", sizeof(value.connectionMessage));
    publish(value);
    Serial.println("Assistant: /assistant.json not configured; local features continue");
    return;
  }
  uint8_t randomBytes[16];
  esp_fill_random(randomBytes, sizeof(randomBytes));
  static constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(randomBytes); ++i) {
    sessionId[i*2] = hex[randomBytes[i] >> 4];
    sessionId[i*2+1] = hex[randomBytes[i] & 0x0f];
  }
  sessionId[32] = 0;
  questions = xQueueCreate(1, sizeof(uint8_t));
  if (!questions) return;
  auto value = snapshot();
  value.configured = true;
  strlcpy(value.status, "Say your wake phrase, or tap Talk", sizeof(value.status));
  publish(value);
  if (xTaskCreate(worker, "assistant-network", 24576, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(questions); questions = nullptr;
    value.configured = false;
    strlcpy(value.status, "Assistant task unavailable", sizeof(value.status));
    publish(value);
  }
}
int requestArtwork(HTTPClient &http, WiFiClient &socket, const char *videoId, const char *source) {
  if (!snapshot().configured || !startHttp(http, socket, "/artwork")) return -1;
  http.setTimeout(14000);
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["video_id"] = videoId;
  doc["source"] = source;
  String body;
  serializeJson(doc, body);
  return http.POST(body);
}
bool askLast(bool music, bool answeringPrompt, bool homeTurn) {
  // Called from the Arduino loop: reserve metadata without copying the 2 KB
  // transcript/reply buffers onto its small stack.
  const auto audio = Sound::snapshot();
  if (!questions || !audio.hasRecording || audio.route != Sound::Route::Idle) return false;
  portENTER_CRITICAL(&stateLock);
  const bool accepted = shared.configured && !shared.busy && !shared.replyReady;
  if (accepted) {
    homeCanceled.store(false);
    shared.homeTurn = homeTurn && !music;
    shared.music = music;
    shared.prompt = false;
    shared.busy = shared.visible = true;
    shared.transcript[0] = shared.reply[0] = 0;
    strlcpy(shared.status, "Preparing question", sizeof(shared.status));
    ++shared.revision;
  }
  portEXIT_CRITICAL(&stateLock);
  if (!accepted) return false;
  const uint8_t request = music ? MusicQuestion : answeringPrompt ? PromptAnswer : Question;
  if (xQueueSend(questions, &request, 0) == pdTRUE) return true;
  updateStatus("Assistant busy", false);
  return false;
}
bool sayFollowUp(bool homeTurn) {
  if (!questions) return false;
  portENTER_CRITICAL(&stateLock);
  const bool accepted = !shared.busy && !shared.replyReady;
  if (accepted) {
    homeCanceled.store(false);
    shared.homeTurn = homeTurn;
    shared.music = false;
    shared.prompt = true;
    shared.busy = true;
    shared.visible = false;
    shared.transcript[0] = shared.reply[0] = 0;
  }
  portEXIT_CRITICAL(&stateLock);
  if (!accepted) return false;
  const uint8_t request = FollowUp;
  if (xQueueSend(questions, &request, 0) == pdTRUE) return true;
  updateStatus("Assistant busy", false);
  return false;
}

void cancelHomeTurn() {
  portENTER_CRITICAL(&stateLock);
  if (shared.homeTurn && (shared.busy || shared.replyReady)) {
    homeCanceled.store(true);
    shared.replyReady = shared.visible = false;
    ++shared.revision;
  }
  portEXIT_CRITICAL(&stateLock);
}

void setConversation(bool active, bool keepRecentContext) {
  const bool wasActive = conversation.load();
  if (active && !wasActive && (!keepRecentContext || !conversationId.load() ||
      uint32_t(millis() - lastConversationEnded) >= 120000)) ++conversationId;
  if (!active && wasActive) lastConversationEnded = millis();
  conversation.store(active);
}

int takeVolumeRequest() { return requestedVolume.exchange(-1); }
void finishVolumeRequest(bool ok) { volumeResult.store(ok ? 1 : -1); }

void clearText() {
  portENTER_CRITICAL(&stateLock);
  if (!shared.busy) {
    shared.visible = false;
    ++shared.revision;
  }
  portEXIT_CRITICAL(&stateLock);
}
void setDeviceStatus(const RemoteSettings::Status &status) {
  portENTER_CRITICAL(&stateLock);
  deviceStatus=status;
  portEXIT_CRITICAL(&stateLock);
}
bool takeCommand(RemoteSettings::Command &command) {
  portENTER_CRITICAL(&stateLock);
  const bool available=commandWaiting;
  if (available) { command=remoteCommand; commandWaiting=false; }
  portEXIT_CRITICAL(&stateLock);
  return available;
}
bool playReadyReply() {
  portENTER_CRITICAL(&stateLock);
  const bool ready=shared.replyReady && !homeCanceled.load();
  portEXIT_CRITICAL(&stateLock);
  if (!ready || !Sound::playAssistantReply()) return false;
  portENTER_CRITICAL(&stateLock);
  shared.replyReady=false;
  strlcpy(shared.status,"AI voice reply",sizeof(shared.status)); ++shared.revision;
  portEXIT_CRITICAL(&stateLock);
  return true;
}

}


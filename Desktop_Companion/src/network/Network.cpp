#include "Network.h"
#include <WiFi.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include <atomic>
namespace {
char networkSsid[33] = {}, networkPassword[65] = {};
std::atomic<bool> started{false};
uint32_t lastAttempt = 0;
}
namespace CompanionNetwork {
void begin(const char *ssid, const char *password) {
  if (started.exchange(true)) return;
  strlcpy(networkSsid, ssid, sizeof(networkSsid));
  strlcpy(networkPassword, password, sizeof(networkPassword));
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(networkSsid, networkPassword);
  lastAttempt = millis();
}
bool beginFromStorage(bool storageAvailable) {
  if (!storageAvailable) { Serial.println("Wi-Fi setup skipped: SD card did not mount"); return false; }
  auto file = SD_MMC.open("/companion.json", FILE_READ);
  if (!file || file.isDirectory() || file.size() > 2048) { Serial.println("ERROR: Wi-Fi configuration /companion.json missing or unreadable"); return false; }
  JsonDocument doc;
  auto error = deserializeJson(doc, file);
  file.close();
  if (error || !doc["ssid"].is<const char *>() || !doc["password"].is<const char *>())
    { Serial.println("ERROR: Wi-Fi configuration JSON or fields invalid"); return false; }
  const char *ssid = doc["ssid"];
  const char *password = doc["password"];
  if (!strlen(ssid) || strlen(ssid) >= sizeof(networkSsid) ||
      strlen(password) >= sizeof(networkPassword)) { Serial.println("ERROR: Wi-Fi configuration field length invalid"); return false; }
  Serial.println("Wi-Fi configuration loaded; connecting");
  begin(ssid, password);
  return true;
}
bool connected() { return WiFi.status() == WL_CONNECTED; }
void update(uint32_t now) {
  if (started && !connected() && uint32_t(now - lastAttempt) >= 15000) {
    lastAttempt = now;
    WiFi.disconnect();
    WiFi.begin(networkSsid, networkPassword);
  }
}
}

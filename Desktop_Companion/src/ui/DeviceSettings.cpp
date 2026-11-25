#include "DeviceSettings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "../display/Display.h"
#include "../audio/Sound.h"
#include "../audio/Microphone.h"
#include "../config/UIConfig.h"
namespace {
DeviceSettings::Values current;
bool changed = false;
uint32_t changedAt = 0;
void mark() { changed = true; changedAt = millis(); }
}
namespace DeviceSettings {
const Values &values() { return current; }
void begin(bool audioAvailable) {
  Preferences prefs;
  if (prefs.begin("companion-ui", true)) {
    current.brightness = constrain(prefs.getInt("brightness", 80), 10, 100);
    current.volume = constrain(prefs.getInt("volume", 85), 0, 100);
    current.microphoneDb = constrain(prefs.getInt("microphone", 0), -12, 12);
    current.palette = constrain(prefs.getInt("palette", 0), 0, 6);
    current.recordSeconds = constrain(prefs.getInt("recordSecs", SoundConfig::RECORD_SECONDS),
                                      int(SoundConfig::MIN_SECONDS), int(SoundConfig::MAX_SECONDS));
    current.lightMode = prefs.getBool("light", false);
    current.wakeEachTurn = prefs.getBool("wakeEachTurn", false);
    current.followUpPrompt = prefs.getBool("followUpPrompt", false);
    current.latitude = prefs.getDouble("latitude", 17.050);
    current.longitude = prefs.getDouble("longitude", 74.267);
    if (!isfinite(current.latitude) || abs(current.latitude)>90) current.latitude=17.050;
    if (!isfinite(current.longitude) || abs(current.longitude)>180) current.longitude=74.267;
    prefs.getString("location", current.location, sizeof(current.location));
    if (!current.location[0]) strlcpy(current.location, "Default location", sizeof(current.location));
    prefs.getString("wakePhrase", current.wakePhrase, sizeof(current.wakePhrase));
    if (!current.wakePhrase[0]) strlcpy(current.wakePhrase, "Hey Sam", sizeof(current.wakePhrase));
    prefs.end();
  }
  if (!Display::setBrightness(current.brightness))
    current.brightness = Display::brightness();
  UIConfig::setLightMode(current.lightMode);
  Sound::setRecordingSeconds(current.recordSeconds);
  if (audioAvailable) {
    if (!Sound::setVolume(current.volume)) Serial.println("Settings: speaker level unavailable");
    if (Sound::snapshot().microphoneReady &&
        !CompanionMicrophone::setLevelDb(current.microphoneDb))
      Serial.println("Settings: microphone level unavailable");
  }
}
bool setBrightness(uint8_t percent) {
  const uint8_t value = constrain(percent, 10, 100);
  if (!Display::setBrightness(value)) return false;
  if (current.brightness != value) { current.brightness = value; mark(); }
  return true;
}
bool setVolume(uint8_t percent) {
  const uint8_t value = min(uint8_t(100), percent);
  if (!Sound::setVolume(value)) return false;
  if (current.volume != value) { current.volume = value; mark(); }
  return true;
}
bool setMicrophoneDb(int8_t db) {
  const auto audio = Sound::snapshot();
  if (!audio.microphoneReady || audio.route == Sound::Route::Recording ||
      Sound::assistantReserved()) return false;
  const int8_t value = constrain(db, -12, 12);
  if (!CompanionMicrophone::setLevelDb(value)) return false;
  if (current.microphoneDb != value) { current.microphoneDb = value; mark(); }
  return true;
}
void setRecordSeconds(uint8_t seconds) {
  const uint8_t value = constrain(int(seconds), int(SoundConfig::MIN_SECONDS), int(SoundConfig::MAX_SECONDS));
  if (Sound::setRecordingSeconds(value) && current.recordSeconds != value) {
    current.recordSeconds = value; mark();
  }
}
void setPalette(uint8_t palette) {
  if (palette <= 6 && current.palette != palette) { current.palette = palette; mark(); }
}
bool apply(const Values &next) {
  const auto audio = Sound::snapshot();
  const Values previous=current;
  const bool brightnessChanged=next.brightness!=previous.brightness;
  const bool volumeChanged=next.volume!=previous.volume;
  const bool microphoneChanged=next.microphoneDb!=previous.microphoneDb;
  const bool phraseChanged=strcmp(next.wakePhrase,previous.wakePhrase)!=0;
  if ((volumeChanged || microphoneChanged || phraseChanged) &&
      (audio.route!=Sound::Route::Idle || audio.teaching || Sound::assistantReserved())) return false;
  // An unchanged audio value must not block unrelated display settings
  // when the SD card, codec or microphone is unavailable.
  const bool applied=(!brightnessChanged || setBrightness(next.brightness)) &&
      (!volumeChanged || setVolume(next.volume)) &&
      (!microphoneChanged || setMicrophoneDb(next.microphoneDb));
  const bool phraseReady=applied && (!phraseChanged || Sound::forgetWake());
  if (!phraseReady) {
    if (brightnessChanged) setBrightness(previous.brightness);
    if (volumeChanged) setVolume(previous.volume);
    if (microphoneChanged) setMicrophoneDb(previous.microphoneDb);
    return false;
  }
  current = next;
  UIConfig::setLightMode(current.lightMode);
  Sound::setRecordingSeconds(current.recordSeconds);
  mark();
  return true;
}
void flush() { if (changed) { changedAt=millis()-1200; update(millis(),false); } }
void update(uint32_t now, bool touching) {
  // Flash is written once after a change settles, never on every slider sample.
  if (!changed || touching || uint32_t(now - changedAt) < 1200) return;
  Preferences prefs;
  bool saved = false;
  if (prefs.begin("companion-ui", false)) {
    saved = prefs.putInt("brightness", current.brightness) &&
            prefs.putInt("volume", current.volume) &&
            prefs.putInt("microphone", current.microphoneDb) &&
            prefs.putInt("palette", current.palette) &&
            prefs.putInt("recordSecs", current.recordSeconds) &&
            prefs.putBool("light", current.lightMode) &&
            prefs.putBool("wakeEachTurn", current.wakeEachTurn) &&
            prefs.putBool("followUpPrompt", current.followUpPrompt) &&
            prefs.putDouble("latitude", current.latitude) &&
            prefs.putDouble("longitude", current.longitude) &&
            prefs.putString("location", current.location) &&
            prefs.putString("wakePhrase", current.wakePhrase);
    prefs.end();
  }
  if (!saved) {
    changedAt = now; // Keep the pending settings for a later retry.
    Serial.println("Settings: save failed; will retry");
    return;
  }
  changed = false;
  Serial.println("Settings saved");
}
}

#pragma once
#include "../config/SoundConfig.h"
namespace DeviceSettings {
struct Values {
  uint8_t brightness = 80, volume = 85, palette = 0, recordSeconds = SoundConfig::RECORD_SECONDS;
  int8_t microphoneDb = 0;
  bool lightMode = false, wakeEachTurn = false, followUpPrompt = false;
  double latitude = 17.050, longitude = 74.267;
  char location[61] = "Default location";
  char wakePhrase[29] = "Hey Sam";
};
void begin(bool audioAvailable);
const Values &values();
bool setBrightness(uint8_t percent);
bool setVolume(uint8_t percent);
bool setMicrophoneDb(int8_t db);
void setPalette(uint8_t palette);
void setRecordSeconds(uint8_t seconds);
bool apply(const Values &values);
void flush();
void update(uint32_t now, bool touching);
}

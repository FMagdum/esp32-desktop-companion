#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include "src/config/BoardPins.h"
#include "src/config/UIConfig.h"
#include "src/drivers/TCA9555.h"
#include "src/drivers/Buttons.h"
#include "src/display/Display.h"
#include "src/touch/Touch.h"
#include "src/face/Face.h"
#include "src/led/LedRing.h"
#include "src/storage/Storage.h"
#include "src/audio/Sound.h"
#include "src/music/Music.h"
#include "src/ui/AppUI.h"
#include "src/assistant/Assistant.h"
#include "src/network/Network.h"

// AppUI snapshots and the synchronous LVGL renderer share the Arduino loop task.
SET_LOOP_TASK_STACK_SIZE(20 * 1024)

static bool touchAvailable = false;
static uint32_t lastTouchError = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nDesktop Companion | Sam Home captions");
  const auto reason = esp_reset_reason();
  const char *reset = reason == ESP_RST_PANIC ? "panic" :
      reason == ESP_RST_BROWNOUT ? "brownout" :
      reason == ESP_RST_TASK_WDT ? "task watchdog" :
      reason == ESP_RST_INT_WDT ? "interrupt watchdog" :
      reason == ESP_RST_WDT ? "watchdog" :
      reason == ESP_RST_POWERON ? "power on" :
      reason == ESP_RST_SW ? "software reset" : "other";
  Serial.printf("Reset reason: %s (%d), free heap=%u\n", reset, int(reason), ESP.getFreeHeap());
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL);
  Wire.setClock(100000);
  if (!TCA9555::initDisplayAndTouchResets()) {
    Serial.println("ERROR: TCA9555 not responding");
    while (true) delay(1000);
  }
  Serial.println("TCA9555 OK");
  Serial.println(Buttons::begin() ? "Buttons ready: Key1 Home / Key2 Music / Key3 Chat" : "Buttons unavailable");
  if (!Display::begin() || !Face::begin()) {
    Serial.println("ERROR: LCD or face buffer initialization failed");
    while (true) delay(1000);
  }
  Serial.println("LCD initialized / face animation ready");
  if (LedRing::begin()) Serial.println("RGB ring initialized: 7 LEDs on GPIO38");
  else Serial.println("ERROR: RGB ring unavailable");
  touchAvailable = Touch::begin();
  Serial.println(touchAvailable ? "Touchscreen ready! FT6336U at 0x38" : "ERROR: FT6336U not detected");
  // Keep the established SD initialization order and the backlight off here.
  // The welcome screen is displayed by AppUI after the hardware is ready.
  const bool storageAvailable = Storage::begin();
  CompanionNetwork::beginFromStorage(storageAvailable);
  CompanionAssistant::begin(storageAvailable);
  Music::begin(storageAvailable); // Configuration reads before audio owns SD access.
  const bool audioAvailable = storageAvailable && Sound::begin();
  Serial.printf("Starting UI: loop stack margin=%u bytes\n",
                unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  AppUI::begin(audioAvailable);
  Display::backlight(true);
  if (audioAvailable) Sound::play(Sound::Effect::Startup);
}
void loop() {
  const uint32_t now = millis();
  CompanionNetwork::update(now);
  static uint32_t lastTouchPoll = 0;
  if (touchAvailable && uint32_t(now - lastTouchPoll) >= 30) {
    lastTouchPoll = now;
    Touch::Point point;
    if (Touch::read(point)) {
      const bool pressed = point.count && point.x < UIConfig::WIDTH && point.y < UIConfig::HEIGHT;
      AppUI::touch(point.x, point.y, pressed, now);
    } else if (uint32_t(now - lastTouchError) >= 1000) {
      Serial.println("Touch I2C read failed");
      lastTouchError = now;
    }
  }
  AppUI::update(millis());
  Face::update(millis());
  LedRing::update(millis(), Face::expression());
}

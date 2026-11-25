#include "Storage.h"
#include <SD_MMC.h>
#include "../config/BoardPins.h"
#include "../drivers/TCA9555.h"

namespace { Storage::Info cached; }
namespace Storage {
Info info() { return cached; }
void refreshUsage() {
  if (!cached.available) return;
  cached.totalBytes = SD_MMC.totalBytes();
  cached.usedBytes = SD_MMC.usedBytes();
}
bool begin() {
  uint8_t output, config;
  // Only EXIO3 is changed; display, touch and USB mux bits are preserved.
  if (!TCA9555::readRegister(0x02, output) ||
      !TCA9555::readRegister(0x06, config) ||
      !TCA9555::writeRegister(0x02, output | BoardPins::SD_CS_MASK) ||
      !TCA9555::writeRegister(0x06, config & uint8_t(~BoardPins::SD_CS_MASK))) {
    Serial.println("ERROR: SD expander setup failed");
    return false;
  }
  // Native SDMMC, 1-bit. Never format a card on failure.
  if (cached.available) return true;
  if (!SD_MMC.setPins(BoardPins::SD_CLK, BoardPins::SD_CMD, BoardPins::SD_D0)) {
    Serial.println("ERROR: microSD pin setup failed");
    return false;
  }
  bool mounted=false;
  constexpr int frequencies[] = {20000, 20000, 10000};
  for (unsigned attempt=0; attempt<3; ++attempt) {
    if (attempt) delay(250);
    Serial.printf("SD mount attempt %u/3 at %d kHz (formatting disabled)\n",attempt+1,frequencies[attempt]);
    if (SD_MMC.begin("/sdcard",true,false,frequencies[attempt]) && SD_MMC.cardType()!=CARD_NONE) {
      mounted=true;
      break;
    }
    SD_MMC.end();
  }
  if (!mounted) {
    Serial.println("ERROR: microSD filesystem did not mount; card contents were not formatted");
    Serial.println("Wi-Fi configuration and SD audio are unavailable until the card mounts");
    return false;
  }
  cached.available = true;
  refreshUsage();
  Serial.printf("microSD mounted: %llu MB\n",
                (unsigned long long)(SD_MMC.cardSize() / (1024 * 1024)));
  return true;
}
}

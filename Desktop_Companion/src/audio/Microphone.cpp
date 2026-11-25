#include <Arduino.h>
#include "Microphone.h"
#include "../drivers/es7210/es7210.h"
#include "../config/BoardPins.h"
#include "../config/SoundConfig.h"

namespace { es7210_dev_handle_t microphone = nullptr; }
namespace CompanionMicrophone {
bool begin() {
  es7210_i2c_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.i2c_addr = BoardPins::ES7210_ADDR;
  if (es7210_new_codec(&bus, &microphone) != ESP_OK) {
    Serial.println("ERROR: ES7210 handle allocation failed");
    return false;
  }
  es7210_codec_config_t config = {};
  config.sample_rate_hz = SoundConfig::SAMPLE_RATE;
  config.mclk_ratio = 256;
  config.i2s_format = ES7210_I2S_FMT_I2S;
  config.bit_width = ES7210_I2S_BITS_16B;
  config.mic_bias = ES7210_MIC_BIAS_2V87;
  config.mic_gain = static_cast<es7210_mic_gain_t>(SoundConfig::MIC_GAIN_CODE);
  config.flags.tdm_enable = false;
  const esp_err_t result = es7210_config_codec(microphone, &config);
  const esp_err_t volume = result == ESP_OK ?
                          es7210_config_volume(microphone, 0) : result;
  if (result != ESP_OK || volume != ESP_OK) {
    es7210_del_codec(microphone);
    microphone = nullptr;
    Serial.println("ERROR: ES7210 library initialization failed; recording disabled");
    return false;
  }
  Serial.println("Microphone ready: ES7210 library, stereo PCM16 at 16000Hz");
  return true;
}
bool setLevelDb(int8_t db) {
  if (!microphone || db < -12 || db > 12) return false;
  return es7210_config_volume(microphone, db) == ESP_OK;
}
}


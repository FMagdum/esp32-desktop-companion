#pragma once
#include <Arduino.h>

namespace SoundConfig {
constexpr char ASSISTANT_REPLY_PATH[] = "/assistant/reply.wav";
constexpr char ASSISTANT_PENDING_PATH[] = "/assistant/reply.tmp";
constexpr uint32_t RECORD_SECONDS = 20;
constexpr uint8_t MIC_GAIN_CODE = 8; // ES7210: 24dB, 3dB steps up to code 11
constexpr char RECORD_PATH[] = "/recordings/last.wav";
constexpr char PENDING_RECORD_PATH[] = "/recordings/pending.wav";
constexpr char PREVIOUS_RECORD_PATH[] = "/recordings/previous.wav";
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr int CODEC_VOLUME = 85; // ES8311 scale 0..100; start at modest volume
constexpr int PCM_GAIN_PERCENT = 50;
constexpr uint32_t MIN_SECONDS = 10;
constexpr uint32_t MAX_SECONDS = 20;
constexpr char STARTUP_PATH[] = "/sounds/startup.wav";
constexpr char TOUCH_PATH[] = "/sounds/touch.wav";
}


#include "../alarm/Alarms.h"
#include "Sound.h"
#include "WakePhrase.h"
#include "SpeechTiming.h"
#include "VoiceCue.h"
#include <Preferences.h>
#include <math.h>
#include "Wav.h"
#include "PcmWav.h"
#include "Microphone.h"
#include <string.h>
#include <atomic>
#include <SD_MMC.h>
#include <ESP_I2S.h>
#include <es8311.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../config/BoardPins.h"
#include "../config/SoundConfig.h"
#include "../drivers/TCA9555.h"

namespace {
I2SClass *output = nullptr;
QueueHandle_t requests = nullptr, controls = nullptr;
// Serialize command admission and SD lease requests. Otherwise a command can
// slip into the queue while the assistant is acquiring the card and be lost.
SemaphoreHandle_t admission = nullptr;
es8311_handle_t codec = nullptr;
enum class Control : uint8_t { ToggleRecording, PlayRecording, PlayAssistant, Teach, Chat, Music, StopVoice, TestWake, ForgetWake };
enum class Lease : uint8_t { None, Requested, Ready, Denied };
Lease assistantLease = Lease::None;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Sound::Snapshot sharedState;
std::atomic<uint8_t> recordingSeconds{SoundConfig::RECORD_SECONDS};

using VoicePage = Sound::VoicePage;
std::atomic<VoicePage> voicePage{VoicePage::Off}, finishedVoice{VoicePage::Off};
std::atomic<bool> followUpEnabled{false};
std::atomic<uint32_t> canceledHomeWake{0}, finishedHomeWake{0};
std::atomic<bool> processingTone{false}, wakeEnabled{false}, cancelVoicePending{false};
bool alarmVolumeActive = false; // Main loop only.
uint8_t speakerVolume = SoundConfig::CODEC_VOLUME; // Main loop only.
constexpr uint8_t AlarmMinVolume = 60;
WakePhrase detector;
constexpr int BLOCK_FRAMES = 128;
constexpr int PRE_BLOCKS = WakePhrase::MaxFrames * WakePhrase::Samples / BLOCK_FRAMES + 6;
constexpr unsigned FOLLOW_UP_BLOCKS = 24, ONSET_BLOCKS = 10;
using MicBlock = int16_t[BLOCK_FRAMES * 2];
MicBlock fallbackPreRoll[FOLLOW_UP_BLOCKS];
bool storeWake(bool write) {
  Preferences prefs;
  if (!prefs.begin("sam-voice", !write)) return false;
  auto &profile = detector.profile;
  const bool ok = write ? prefs.putBytes("wake1",&profile,sizeof(profile)) == sizeof(profile) :
      prefs.getBytesLength("wake1")==sizeof(profile) &&
      prefs.getBytes("wake1",&profile,sizeof(profile))==sizeof(profile) && detector.valid();
  prefs.end();
  return ok;
}

void publish(Sound::Snapshot state) {
  portENTER_CRITICAL(&stateLock);
  state.revision = sharedState.revision + 1;
  sharedState = state;
  portEXIT_CRITICAL(&stateLock);
}
bool sendControl(Control control) {
  if (!controls || !admission || xSemaphoreTake(admission, 0) != pdTRUE) return false;
  const bool sent = !Sound::assistantReserved() && xQueueSend(controls, &control, 0) == pdTRUE;
  xSemaphoreGive(admission);
  return sent;
}
bool takeControl(Control &control) {
  if (xSemaphoreTake(admission, 0) != pdTRUE) return false;
  const bool received = !Sound::assistantReserved() && xQueueReceive(controls, &control, 0) == pdTRUE;
  xSemaphoreGive(admission);
  return received;
}
bool takeEffect(Sound::Effect &effect) {
  if (xSemaphoreTake(admission, 0) != pdTRUE) return false;
  const bool received = !Sound::assistantReserved() && xQueueReceive(requests, &effect, 0) == pdTRUE;
  xSemaphoreGive(admission);
  return received;
}
void acknowledgeLease(bool idle) {
  // A command already accepted by the UI must run before a new lease can be
  // granted. The admission mutex prevents later commands from joining it.
  const bool available = idle && !uxQueueMessagesWaiting(controls) &&
                         !uxQueueMessagesWaiting(requests);
  portENTER_CRITICAL(&stateLock);
  if (assistantLease == Lease::Requested)
    assistantLease = available ? Lease::Ready : Lease::Denied;
  portEXIT_CRITICAL(&stateLock);
}

bool amplifier(bool enabled) {
  uint8_t value, config;
  if (!TCA9555::readRegister(0x03, value) ||
      !TCA9555::readRegister(0x07, config)) return false;
  value = enabled ? value | BoardPins::SPEAKER_ENABLE_MASK
                  : value & uint8_t(~BoardPins::SPEAKER_ENABLE_MASK);
  return TCA9555::writeRegister(0x03, value) &&
         TCA9555::writeRegister(0x07, config & uint8_t(~BoardPins::SPEAKER_ENABLE_MASK));
}

// Two chimes followed by a pause. Mix in PCM so an alarm can interrupt
// the listener without changing the state of an in-progress playback.
void mixAlarm(int16_t *pcm, size_t frames, unsigned &position) {
  constexpr unsigned rate = SoundConfig::SAMPLE_RATE;
  constexpr unsigned cycle = 2 * rate;
  constexpr unsigned note = rate / 5;
  constexpr unsigned secondNote = rate * 3 / 10;
  for (size_t frame = 0; frame < frames; ++frame) {
    unsigned phase = position;
    if (++position == cycle) position = 0;
    unsigned frequency = 784;
    if (phase >= secondNote) {
      phase -= secondNote;
      frequency = 988;
    }
    if (phase >= note) continue;
    const float envelope = std::min(1.f, std::min(phase, note - 1 - phase) / 128.f);
    const int sample = int(10000 * envelope * sinf(6.2831853f * frequency * phase / rate));
    for (size_t channel = 0; channel < 2; ++channel) {
      auto &value = pcm[frame * 2 + channel];
      value = constrain(int(value) + sample, -32768, 32767);
    }
  }
}

// Only this audio task reads/writes recording files and I2S after initialization.
void playbackTask(void *) {
  fs::File file;
  Wav::Info info;
  auto state = Sound::snapshot();
  uint32_t remaining = 0, captured = 0, settling = 0, lastPublish = 0;
  uint32_t recordStarted = 0;
  int16_t pcm[256], microphone[256]; // 128 stereo frames, 8ms at 16kHz
  uint8_t input[512], header[44];
  bool recording = false;
  uint8_t activeRecordSeconds = SoundConfig::RECORD_SECONDS;
  uint32_t maxBytes = SoundConfig::SAMPLE_RATE * 4 * activeRecordSeconds;

  VoicePage capture = VoicePage::Off, lastPage = VoicePage::Off;
  VoiceCue cue;
  unsigned guardFrames=0, alarmPosition=0;
  bool wasRinging=false, alarmAudible=false;
  auto *preRoll = static_cast<MicBlock *>(ps_malloc(PRE_BLOCKS * sizeof(MicBlock)));
  const unsigned ringBlocks = preRoll ? PRE_BLOCKS : FOLLOW_UP_BLOCKS;
  if (!preRoll) {
    preRoll = fallbackPreRoll;
    Serial.println("Audio: PSRAM unavailable, using short wake pre-roll");
  }
  unsigned preIndex=0, preCount=0, monoCount=0, speechBlocks=0;
  unsigned firstSpeech=0, lastSpeech=0, wakeFrame=0;
  bool wakeCapture=false;
  uint32_t testStarted=0;
  float ambient=35;
  unsigned loudBlocks=0, onsetGap=0;
  int16_t mono[WakePhrase::Samples];
  auto message = [&](const char *text) {
    strlcpy(state.message, text, sizeof(state.message));
    Serial.printf("Audio: %s\n", text);
    publish(state);
  };
  auto finish = [&](bool success, bool wakeTimedOut=false, bool canceled=false) {
    bool quiet = capture != VoicePage::Off && speechBlocks < 6;
    bool saved = success && captured > 0 && !quiet;
    if (saved) {
      PcmWav::header(header, captured, SoundConfig::SAMPLE_RATE, 2);
      saved = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
      file.flush();
    }
    file.close();
    if (saved) {
      // Keep the last successful recording until the new WAV is complete.
      if (SD_MMC.exists(SoundConfig::PREVIOUS_RECORD_PATH) &&
          !SD_MMC.remove(SoundConfig::PREVIOUS_RECORD_PATH)) saved = false;
      const bool oldExists = SD_MMC.exists(SoundConfig::RECORD_PATH);
      if (saved && oldExists &&
          !SD_MMC.rename(SoundConfig::RECORD_PATH, SoundConfig::PREVIOUS_RECORD_PATH))
        saved = false;
      if (saved && !SD_MMC.rename(SoundConfig::PENDING_RECORD_PATH, SoundConfig::RECORD_PATH)) {
        saved = false;
        if (oldExists && !SD_MMC.rename(SoundConfig::PREVIOUS_RECORD_PATH, SoundConfig::RECORD_PATH))
          Serial.println("Audio: previous WAV preserved as /recordings/previous.wav");
      }
    } else {
      SD_MMC.remove(SoundConfig::PENDING_RECORD_PATH);
    }
    recording = false;
    state.route = Sound::Route::Idle;
    state.hasRecording = SD_MMC.exists(SoundConfig::RECORD_PATH);
    state.recordedMs = captured * 1000 / (SoundConfig::SAMPLE_RATE * 4);
    Serial.printf("Audio capture: %lu ms, peak L=%u R=%u\n",
                  (unsigned long)state.recordedMs, state.peakLeft, state.peakRight);
    const VoicePage completed=capture;
    state.musicCaptureFailed = !saved && completed==VoicePage::Music;
    capture=VoicePage::Off;
    if (canceled) {
      state.message[0]=0;
      publish(state);
    } else if (wakeTimedOut) {
      ++state.wakeTimeouts;
      state.message[0]=0;
      publish(state);
    } else {
      message(saved ? "Recording saved" : quiet ? "No speech heard; try again" : "Recording failed; previous kept");
    }
    if (saved && completed!=VoicePage::Off) {
      finishedHomeWake.store(completed==VoicePage::Home && wakeCapture ? state.wakeCount : 0);
      finishedVoice.store(completed);
    }
    guardFrames=1600;
  };
  auto finishReplyPlayback = [&](bool succeeded) {
    state.replyPlaybackSucceeded = succeeded;
    ++state.replyPlaybackEvents;
  };
  auto openPlayback = [&](const char *path, bool isRecording, bool aiReply = false) {
    if (file) file.close();
    remaining = 0;
    file = SD_MMC.open(path, FILE_READ);
    if (!file || file.isDirectory() || !Wav::parse(file, info, aiReply ? 30 : SoundConfig::MAX_SECONDS)) {
      if (file) file.close();
      state.route = Sound::Route::Idle;
      if (aiReply) finishReplyPlayback(false);
      message("Missing or invalid WAV");
      return;
    }
    remaining = info.dataBytes;
    if (aiReply) {
      state.replyPlayedFrames = 0;
      state.replyTotalFrames = info.dataBytes / (info.channels * 2);
    }
    state.route = aiReply ? Sound::Route::AssistantPlayback : isRecording ? Sound::Route::RecordedPlayback : Sound::Route::Effect;
    message(aiReply ? "Speaking AI reply" : isRecording ? "Playing last recording" : "Playing effect");
  };

  auto beginCapture = [&](VoicePage origin, bool detected, bool followUp = false) {
    state.musicCaptureFailed = origin==VoicePage::Music;
    state.wakeTest=0;
    if (file) file.close();
    remaining = 0;
    if (!SD_MMC.exists("/recordings") && !SD_MMC.mkdir("/recordings")) {
      state.route = Sound::Route::Idle;
      message("Cannot create recordings folder");
    } else {
      file = SD_MMC.open(SoundConfig::PENDING_RECORD_PATH, FILE_WRITE);
      PcmWav::header(header, 0, SoundConfig::SAMPLE_RATE, 2);
      if (!file || file.write(header, sizeof(header)) != sizeof(header)) {
        if (file) file.close();
        state.route = Sound::Route::Idle;
        message("Cannot open recording on SD");
      } else {
        recording = true;
        state.musicCaptureFailed=false;
        capture = origin;
        wakeCapture = detected;
        speechBlocks = 0;
        firstSpeech = lastSpeech = 0;
        activeRecordSeconds = recordingSeconds.load();
        maxBytes = SoundConfig::SAMPLE_RATE * 4 * activeRecordSeconds;
        captured = 0;
        settling = SoundConfig::SAMPLE_RATE / 5; // Discard 200ms of speaker/DMA tail.
        if (detected) {
          ++state.wakeCount;
          state.wakeOrigin=origin;
        }
        if (detected || followUp) {
          // Keep the full recognized phrase for the bridge to strip and the
          // start of any question already arriving in the same breath.
          const unsigned retained = detected ?
              (detector.matchedSamples() + BLOCK_FRAMES - 1) / BLOCK_FRAMES + 4 : FOLLOW_UP_BLOCKS;
          const unsigned blocks = std::min(preCount, retained);
          for (unsigned i=0;i<blocks;++i) {
            const auto &block=preRoll[(preIndex+ringBlocks-blocks+i)%ringBlocks];
            if (file.write((const uint8_t*)block,sizeof(block))!=sizeof(block)) { finish(false); return; }
            captured+=sizeof(block);
          }
          settling=0;
          if (followUp) {
            speechBlocks = ONSET_BLOCKS;
            lastSpeech = captured / 4;
            firstSpeech = lastSpeech - std::min(lastSpeech, ONSET_BLOCKS * unsigned(BLOCK_FRAMES));
          }
        }
        detector.reset(); monoCount=preCount=preIndex=loudBlocks=onsetGap=0;
        wakeFrame = captured / 4;
        recordStarted = millis();
        state.route = Sound::Route::Recording;
        state.recordedMs = 0;
        state.peakLeft = state.peakRight = 0;
        message(detected ? "Wake detected - listening" : "Listening");
      }
    }
  };
  for (;;) {
    if (recording && capture==VoicePage::Home && wakeCapture && canceledHomeWake.load()==state.wakeCount)
      finish(false,false,true);
    acknowledgeLease(!recording && !remaining && !file);
    Control control;
    if (takeControl(control)) {
      if (control == Control::ForgetWake) {
        detector.profile={}; detector.reset();
        Preferences prefs;
        if (prefs.begin("sam-voice",false)) { prefs.remove("wake1"); prefs.end(); }
        state.wakeTrained=false; state.teaching=false; state.wakeTest=0;
        message("Wake phrase needs enrollment");
      } else if (control == Control::TestWake && state.wakeTrained && !recording && !remaining) {
        state.wakeTest=1; testStarted=millis(); detector.reset();
        message("Wake test: say your phrase");
      } else if (control == Control::Teach && !recording && !remaining && voicePage.load()==VoicePage::Chat) {
        state.teaching=true; state.wakeTest=0; detector.reset(); monoCount=0;
        message("Say your wake phrase, then pause");
      } else if ((control==Control::Chat || control==Control::Music) && !recording && !state.teaching && !remaining) {
        beginCapture(control==Control::Chat ? VoicePage::Chat : VoicePage::Music,false);
      } else if (control==Control::StopVoice) {
        if (recording && capture!=VoicePage::Off) finish(true);
      } else if (control == Control::ToggleRecording && state.microphoneReady) {
        if (recording) {
          finish(true);
        } else {
          beginCapture(VoicePage::Off, false);
        }
      } else if (control == Control::PlayRecording && !recording) {
        openPlayback(SoundConfig::RECORD_PATH, true);
      } else if (control == Control::PlayAssistant) {
        if (recording) finishReplyPlayback(false);
        else openPlayback(SoundConfig::ASSISTANT_REPLY_PATH, false, true);
      }
    }
    // A Home cancel is accepted even while the control queue is busy.
    if (cancelVoicePending.exchange(false)) {
      if (recording && capture != VoicePage::Off) finish(false, false, true);
      else if (state.route == Sound::Route::AssistantPlayback) {
        file.close();
        remaining = 0;
        state.route = Sound::Route::Idle;
        guardFrames = 3200;
        finishReplyPlayback(false);
        message("Reply canceled");
      }
      if (uxQueueMessagesWaiting(controls)) cancelVoicePending.store(true);
    }
    Sound::Effect effect;
    if (takeEffect(effect) &&
        !recording && state.route != Sound::Route::RecordedPlayback &&
        state.route != Sound::Route::AssistantPlayback) {
      openPlayback(effect == Sound::Effect::Startup ?
                   SoundConfig::STARTUP_PATH : SoundConfig::TOUCH_PATH, false);
    }

    if (processingTone.exchange(false)) {
      cue.start(VoiceCue::Processing);
      guardFrames=3136;
      Serial.println("Voice: processing cue");
    }
    memset(pcm, 0, sizeof(pcm));
    size_t frames = 128;
    const bool playedFile = remaining!=0;
    const bool playingReply = state.route == Sound::Route::AssistantPlayback;
    bool replyEnded = false;
    if (remaining) {
      const uint32_t frameBytes = info.channels * 2;
      frames = min(uint32_t(128), remaining / frameBytes);
      const size_t bytes = frames * frameBytes;
      if (file.read(input, bytes) != bytes) {
        remaining = 0;
        file.close();
        state.route = Sound::Route::Idle;
        frames = 128;
        if (playingReply) finishReplyPlayback(false);
        message("SD playback read failed");
      } else {
        for (size_t frame = 0; frame < frames; ++frame) {
          for (size_t channel = 0; channel < 2; ++channel) {
            const size_t index = (frame * info.channels + (info.channels == 1 ? 0 : channel)) * 2;
            const int16_t sample = int16_t(uint16_t(input[index]) | uint16_t(input[index+1]) << 8);
            pcm[frame*2+channel] = int32_t(sample) * SoundConfig::PCM_GAIN_PERCENT / 100;
          }
        }
        remaining -= bytes;
        if (!remaining) {
          file.close();
          state.route = Sound::Route::Idle;
          replyEnded = playingReply;
          message("Playback finished");
        }
      }
    }
    const bool ringing = Alarms::isRinging();
    if (!ringing) alarmAudible = false;
    if (wasRinging && !ringing) guardFrames = 3200;
    wasRinging = ringing;
    const bool speakerActive = ringing || playedFile || state.route != Sound::Route::Idle || guardFrames || cue.active();
    cue.render(pcm,frames);
    // Keep the current recording clean. The chime begins when capture ends.
    if (ringing && !recording) mixAlarm(pcm, frames, alarmPosition);
    else alarmPosition = 0;
    const size_t transfer = frames * 4;
    if (output->write((const uint8_t *)pcm, transfer) != transfer) {
      if (recording) finish(false);
      else {
        if (file) file.close();
        remaining = 0;
        state.route = Sound::Route::Idle;
        if (playingReply) finishReplyPlayback(false);
        message("I2S output failed");
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      if (ringing && !recording && !alarmAudible) {
        alarmAudible = true;
        Serial.println("Alarms: chime sent to the speaker");
      }
      if (playingReply && playedFile && (replyEnded || state.route == Sound::Route::AssistantPlayback)) {
        state.replyPlayedFrames = state.replyTotalFrames - remaining / (info.channels * 2);
        if (replyEnded) {
          finishReplyPlayback(true);
          guardFrames=3200;
        }
      }
    }
    // Drain RX continuously, including during playback/idle, to avoid storing
    // samples captured before the user pressed RECORD.
    const size_t received = output->rxChan() ? output->readBytes((char *)microphone, transfer) : 0;
    const VoicePage currentPage=voicePage.load();
    if (state.wakeTest==1 && (uint32_t(millis()-testStarted)>=10000 || currentPage!=VoicePage::Chat)) {
      state.wakeTest=3; message("Wake test finished; no match");
    }
    const auto voice = !speakerActive || recording ? SpeechTiming::measure(microphone, received / 4) :
        SpeechTiming::Frame{};
    const bool speech = voice.shaped && voice.level > std::max(70.f,ambient*2.5f);
    if (!recording && !speakerActive && received == transfer && voice.level < ambient * 2.5f)
      ambient = ambient * 0.99f + voice.level * 0.01f;
    if (currentPage!=lastPage) {
      detector.reset(); monoCount=preCount=preIndex=loudBlocks=onsetGap=0; lastPage=currentPage;
      if (state.teaching && currentPage!=VoicePage::Chat) {
        state.teaching=false; message("Wake setup canceled");
      }
    }
    const bool followUp = followUpEnabled.load() && currentPage != VoicePage::Off &&
                          !state.teaching && state.wakeTest != 1;
    if (!recording && state.microphoneReady && currentPage!=VoicePage::Off &&
        (followUp || state.teaching || (state.wakeTrained && wakeEnabled.load())) && !speakerActive && !Sound::assistantReserved() &&
        finishedVoice.load()==VoicePage::Off && received==sizeof(microphone)) {
      memcpy(preRoll[preIndex],microphone,sizeof(microphone));
      preIndex=(preIndex+1)%ringBlocks; preCount=std::min(ringBlocks,preCount+1);
      if (followUp) {
        if (speech) { ++loudBlocks; onsetGap = 0; }
        else if (++onsetGap >= 4) { loudBlocks = 0; onsetGap = 4; }
        if (loudBlocks >= ONSET_BLOCKS) {
          loudBlocks = onsetGap = 0;
          beginCapture(VoicePage::Chat, false, true);
        }
        continue;
      }
      int left=0,right=0;
      for (size_t i=0;i<frames;++i) { left+=abs(int(microphone[2*i])); right+=abs(int(microphone[2*i+1])); }
      for (size_t i=0;i<frames;++i) mono[monoCount++]=microphone[2*i+(right>left)];
      if (monoCount==WakePhrase::Samples) {
        monoCount=0;
        auto event=detector.feed(mono,state.teaching);
        if (event==WakePhrase::Event::Match && voicePage.load()==currentPage) {
          if (state.wakeTest==1) {
            state.wakeTest=2;
            cue.start(VoiceCue::Wake); guardFrames=2624;
            message("Wake test: detected");
            continue;
          }
          beginCapture(currentPage, true);
          continue; // The current RX block is already in pre-roll.
        }
        if (event==WakePhrase::Event::Learned || event==WakePhrase::Event::Retry) {
          state.teaching=false;
          if (event==WakePhrase::Event::Learned) {
            state.wakeTrained=storeWake(true);
            message(state.wakeTrained ? "Wake phrase learned" : "Wake profile save failed");
          } else message("Try again: say your phrase clearly");
        }
      }
    } else {
      monoCount=preCount=preIndex=loudBlocks=onsetGap=0;
      detector.reset();
    }
    if (guardFrames) guardFrames=guardFrames>frames ? guardFrames-frames : 0;
    if (recording) {
      if (received != transfer || received % 4) {
        finish(false);
        vTaskDelay(pdMS_TO_TICKS(10));
      } else if (settling) {
        // Discard Pod's voice and its speaker tail. Start the silence timer
        // once the microphone can capture the user's response.
        recordStarted=millis();
        settling = settling > frames ? settling - frames : 0;
      } else {
        const size_t bytes = min(uint32_t(received), maxBytes - captured);
        if (capture != VoicePage::Off && speech && !guardFrames) {
          if (!speechBlocks) firstSpeech = captured / 4;
          ++speechBlocks;
          lastSpeech = (captured + bytes) / 4;
        }
        if (file.write((const uint8_t *)microphone, bytes) != bytes) {
          finish(false);
        } else {
          captured += bytes;
          state.recordedMs = captured * 1000 / (SoundConfig::SAMPLE_RATE * 4);
          for (size_t sample = 0; sample < bytes/2; sample += 2) {
            const uint16_t left = abs(int32_t(microphone[sample]));
            const uint16_t right = abs(int32_t(microphone[sample+1]));
            state.peakLeft = max(state.peakLeft, left);
            state.peakRight = max(state.peakRight, right);
          }
          if (captured >= maxBytes || (capture != VoicePage::Off && speechBlocks >= 6 &&
              SpeechTiming::canEndTurn(captured / 4, firstSpeech, lastSpeech, wakeFrame, wakeCapture))) finish(true);
        }
      }
      if (recording && wakeCapture && speechBlocks < 6 && uint32_t(millis()-recordStarted)>=3000)
        finish(false,true);
      // Wall-clock failsafe if card/I2S operations stop delivering usable samples.
      if (recording && uint32_t(millis()-recordStarted) > (activeRecordSeconds+3)*1000)
        finish(false);
    }
    if (uint32_t(millis()-lastPublish) >= 100) {
      lastPublish = millis();
      publish(state);
    }
  }
}
}
namespace Sound {
bool begin() {
  if (!amplifier(false)) {
    Serial.println("ERROR: Speaker amplifier control failed");
    return false;
  }
  static_assert(SoundConfig::RECORD_SECONDS > 0 &&
                SoundConfig::RECORD_SECONDS <= SoundConfig::MAX_SECONDS,
                "Recording length must fit supported WAV playback duration");
  static I2SClass i2s;
  output = &i2s;
  output->setPins(BoardPins::AUDIO_BCLK, BoardPins::AUDIO_LRCK,
                  BoardPins::AUDIO_DOUT, BoardPins::AUDIO_DIN, BoardPins::AUDIO_MCLK);
  bool captureEnabled = true;
  if (!output->begin(I2S_MODE_STD, SoundConfig::SAMPLE_RATE,
                     I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Full-duplex I2S unavailable; trying speaker-only mode");
    output->end();
    output->setPins(BoardPins::AUDIO_BCLK, BoardPins::AUDIO_LRCK,
                    BoardPins::AUDIO_DOUT, -1, BoardPins::AUDIO_MCLK);
    captureEnabled = false;
    if (!output->begin(I2S_MODE_STD, SoundConfig::SAMPLE_RATE,
                       I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      Serial.println("ERROR: Speaker I2S initialization failed");
      return false;
    }
  }
  codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  es8311_clock_config_t clock = {};
  clock.mclk_from_mclk_pin = true;
  clock.mclk_frequency = SoundConfig::SAMPLE_RATE * 256;
  clock.sample_frequency = SoundConfig::SAMPLE_RATE;
  if (!codec || es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_voice_volume_set(codec, SoundConfig::CODEC_VOLUME, nullptr) != ESP_OK ||
      es8311_voice_mute(codec, false) != ESP_OK) {
    Serial.println("ERROR: ES8311 initialization failed");
    if (codec) es8311_delete(codec);
    codec = nullptr;
    output->end();
    return false;
  }
  output->setTimeout(100);
  const bool micReady = captureEnabled && CompanionMicrophone::begin();
  controls = xQueueCreate(2, sizeof(Control));
  requests = xQueueCreate(1, sizeof(Effect));
  admission = xSemaphoreCreateMutex();
  if (!requests || !controls || !admission || !amplifier(true)) {
    if (requests) vQueueDelete(requests);
    requests = nullptr;
    if (controls) vQueueDelete(controls);
    controls = nullptr;
    if (admission) vSemaphoreDelete(admission);
    admission = nullptr;
    amplifier(false);
    es8311_delete(codec);
    codec = nullptr;
    output->end();
    Serial.println("ERROR: Sound queue/amplifier initialization failed");
    return false;
  }
  Snapshot initial;
  initial.microphoneReady = micReady;
  initial.wakeTrained = micReady && storeWake(false);
  initial.hasRecording = SD_MMC.exists(SoundConfig::RECORD_PATH);
  strlcpy(initial.message, micReady ? "Audio ready" : "Microphone unavailable", sizeof(initial.message));
  publish(initial);
  if (xTaskCreate(playbackTask, "sound-playback", 12288, nullptr, 2, nullptr) != pdPASS) {
    amplifier(false);
    vQueueDelete(requests);
    requests = nullptr;
    if (controls) vQueueDelete(controls);
    controls = nullptr;
    if (admission) vSemaphoreDelete(admission);
    admission = nullptr;
    es8311_delete(codec);
    codec = nullptr;
    output->end();
    initial.microphoneReady = false;
    strlcpy(initial.message, "Audio task unavailable", sizeof(initial.message));
    publish(initial);
    Serial.println("ERROR: Sound task creation failed");
    return false;
  }
  Serial.println("Sound ready: ES8311, 16000Hz PCM16");
  return true;
}

void play(Effect effect) {
  const auto state = snapshot();
  if (!requests || !admission || xSemaphoreTake(admission, 0) != pdTRUE) return;
  if (!assistantReserved() && state.route != Route::Recording &&
      state.route != Route::RecordedPlayback && state.route != Route::AssistantPlayback)
    xQueueOverwrite(requests, &effect);
  xSemaphoreGive(admission);
}

bool setRecordingSeconds(uint8_t seconds) {
  if (seconds < SoundConfig::MIN_SECONDS || seconds > SoundConfig::MAX_SECONDS) return false;
  recordingSeconds.store(seconds);
  return true;
}
bool setVolume(uint8_t percent) {
  if (!codec || !controls) return false;
  const uint8_t volume = min(uint8_t(100), percent);
  const uint8_t applied = alarmVolumeActive ? max(volume, AlarmMinVolume) : volume;
  if (es8311_voice_volume_set(codec, applied, nullptr) != ESP_OK) return false;
  speakerVolume = volume;
  return true;
}
void setAlarmRinging(bool ringing) {
  if (alarmVolumeActive == ringing) return;
  alarmVolumeActive = ringing;
  if (!setVolume(speakerVolume)) Serial.println("Alarms: speaker level unavailable");
}
Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  Snapshot result = sharedState;
  portEXIT_CRITICAL(&stateLock);
  return result;
}
void setVoicePage(VoicePage page, bool enabled) { wakeEnabled.store(enabled); voicePage.store(page); }
void cancelVoice() { cancelVoicePending.store(true); }
void setFollowUpListening(bool enabled) { followUpEnabled.store(enabled); }
void cancelHomeWake(uint32_t wakeCount) { canceledHomeWake.store(wakeCount); }
VoicePage takeVoiceRecording() {
  const auto finished=finishedVoice.exchange(VoicePage::Off);
  const uint32_t wake=finishedHomeWake.load();
  if (finished==VoicePage::Home && wake && wake==canceledHomeWake.load()) return VoicePage::Off;
  return finished;
}
void processingCue() { processingTone.store(true); }
bool prepareSleep() {
  return reserveForAssistant() && amplifier(false);
}
bool forgetWake() {
  return snapshot().route==Route::Idle && !assistantReserved() && sendControl(Control::ForgetWake);
}
bool testWake() {
  const auto value=snapshot();
  return value.wakeTrained && !value.teaching && value.route==Route::Idle &&
      voicePage.load()==VoicePage::Chat && sendControl(Control::TestWake);
}
bool teachWake() {
  const auto s=snapshot();
  return s.microphoneReady && s.route==Route::Idle && voicePage.load()==VoicePage::Chat &&
         !s.teaching && sendControl(Control::Teach);
}
bool startVoice(VoicePage page) {
  const auto s=snapshot();
  return s.microphoneReady && !s.teaching && s.route==Route::Idle &&
         (page==VoicePage::Chat || page==VoicePage::Music) &&
         sendControl(page==VoicePage::Music ? Control::Music : Control::Chat);
}
bool stopVoice() { return sendControl(Control::StopVoice); }
bool toggleRecording() {
  return controls && !assistantReserved() && snapshot().microphoneReady &&
         sendControl(Control::ToggleRecording);
}
bool playRecording() {
  const auto state = snapshot();
  return controls && !assistantReserved() && state.hasRecording && state.route != Route::Recording &&
         sendControl(Control::PlayRecording);
}

bool assistantReserved() {
  portENTER_CRITICAL(&stateLock);
  const bool reserved = assistantLease == Lease::Requested || assistantLease == Lease::Ready;
  portEXIT_CRITICAL(&stateLock);
  return reserved;
}
bool reserveForAssistant(uint32_t timeoutMs) {
  if (!controls || !admission || xSemaphoreTake(admission, 0) != pdTRUE) return false;
  portENTER_CRITICAL(&stateLock);
  if (assistantLease != Lease::None) {
    portEXIT_CRITICAL(&stateLock);
    xSemaphoreGive(admission);
    return false;
  }
  assistantLease = Lease::Requested;
  portEXIT_CRITICAL(&stateLock);
  xSemaphoreGive(admission);
  const uint32_t started = millis();
  for (;;) {
    portENTER_CRITICAL(&stateLock);
    const Lease phase = assistantLease;
    if (phase == Lease::Denied) assistantLease = Lease::None;
    if (phase == Lease::Requested && uint32_t(millis()-started) >= timeoutMs)
      assistantLease = Lease::None;
    portEXIT_CRITICAL(&stateLock);
    if (phase == Lease::Ready) return true;
    if (phase == Lease::Denied || uint32_t(millis()-started) >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void releaseAssistant() {
  portENTER_CRITICAL(&stateLock);
  assistantLease = Lease::None;
  portEXIT_CRITICAL(&stateLock);
}
bool playAssistantReply() {
  return controls && !assistantReserved() && sendControl(Control::PlayAssistant);
}
}

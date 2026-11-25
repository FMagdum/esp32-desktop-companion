#pragma once
#include <Arduino.h>
namespace Sound {
enum class Effect { Startup, Touch };
enum class Route : uint8_t { Idle, Effect, Recording, RecordedPlayback, AssistantPlayback };
// Retained for the archived WakeGreetings sample renderer.
enum class WakeGreeting : uint8_t { None, OfferHelp, Hello };
enum class VoicePage : uint8_t { Off, Home, Chat, Music };
struct Snapshot {
  bool wakeTrained = false, teaching = false;
  bool musicCaptureFailed = false;
  uint8_t wakeTest = 0; // 1 listening, 2 detected, 3 timed out
  uint32_t wakeCount = 0, wakeTimeouts = 0, replyPlaybackEvents = 0;
  bool replyPlaybackSucceeded = false;
  VoicePage wakeOrigin = VoicePage::Off;
  Route route = Route::Idle;
  bool microphoneReady = false;
  bool hasRecording = false;
  uint32_t recordedMs = 0, replyPlayedFrames = 0, replyTotalFrames = 0;
  uint16_t peakLeft = 0, peakRight = 0;
  char message[48] = "Audio not initialized";
  uint32_t revision = 0;
};
bool begin(); // Call only after Storage::begin succeeds.
void play(Effect effect);
// Main loop only: shares the Wire bus with touch.
bool setVolume(uint8_t percent);
void setAlarmRinging(bool ringing);
// Applied to the next recording; 10 to 20 seconds.
bool setRecordingSeconds(uint8_t seconds);
void setVoicePage(VoicePage page, bool wakeEnabled);
void setFollowUpListening(bool enabled);
void cancelHomeWake(uint32_t wakeCount);
bool teachWake();
bool testWake();
bool forgetWake();
bool prepareSleep();
bool startVoice(VoicePage page);
VoicePage takeVoiceRecording();
void processingCue();
bool stopVoice();
void cancelVoice();
bool toggleRecording();
bool playRecording();
Snapshot snapshot();
// The worker acknowledges this lease only when no audio file is open.
// While leased, the assistant task alone may access recording/reply files.
bool reserveForAssistant(uint32_t timeoutMs = 3000);
void releaseAssistant();
bool assistantReserved();
bool playAssistantReply();
}

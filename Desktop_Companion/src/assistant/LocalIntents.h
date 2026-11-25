#pragma once
#include <Arduino.h>

namespace LocalIntents {
enum class Action : uint8_t { None, Volume, Music, Clock, Interpret };
struct Reply {
  String text;
  Action action = Action::None;
  int volume = 0;
};
enum class AlarmDay : uint8_t { Next, Today, Tomorrow };
enum class Interpretation : uint8_t { None, Clock, Alarm };
struct Intent {
  Interpretation kind = Interpretation::None;
  AlarmDay day = AlarmDay::Next;
  int delay = 0;
  int hour = -1;
  int minute = 0;
  bool ambiguous = false;
};
void begin();
void clearPendingAlarm();
Reply apply(const Intent &intent);
Reply match(const char *transcript, int volume, bool musicOnline);
}

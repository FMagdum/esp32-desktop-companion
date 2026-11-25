#include "LocalIntents.h"
#include "../music/Music.h"
#include <ArduinoJson.h>
#include "../alarm/Alarms.h"
#include <SD_MMC.h>
#include <time.h>
#include <esp_random.h>

namespace {
JsonDocument intents;
using namespace LocalIntents;
Intent pendingAlarm;
uint32_t pendingAt = 0;
String normalize(const char *text) {
  String command;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    if (isalnum(*p) || *p == ':' || *p == '%') command += char(tolower(*p));
    else if (command.length() && !command.endsWith(" ")) command += ' ';
  }
  command.trim();
  if (command.startsWith("please ")) command.remove(0, 7);
  return command;
}

String variant(JsonArrayConst responses) {
  if (responses.size()) {
    const char *chosen = responses[esp_random() % responses.size()] | "";
    if (strlen(chosen) <= 160) return chosen;
  }
  return {};
}

bool digits(const String &text, unsigned &value) {
  if (!text.length() || text.length() > 4) return false;
  value = 0;
  for (char digit : text) {
    if (digit < '0' || digit > '9') return false;
    value = value * 10 + digit - '0';
  }
  return true;
}

bool number(String text, unsigned &value) {
  text.trim();
  if (digits(text, value)) return true;
  const char *units[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
                         "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
                         "seventeen", "eighteen", "nineteen"};
  for (unsigned i = 0; i < 20; ++i) if (text == units[i]) { value = i; return true; }
  const char *tens[] = {"twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
  for (unsigned i = 0; i < 8; ++i) {
    if (text == tens[i]) { value = (i + 2) * 10; return true; }
    if (!text.startsWith(String(tens[i]) + " ")) continue;
    const auto unit = text.substring(strlen(tens[i]) + 1);
    for (unsigned j = 1; j < 10; ++j) if (unit == units[j]) { value = (i + 2) * 10 + j; return true; }
  }
  if (text == "one hundred" || text == "a hundred") { value = 100; return true; }
  return false;
}

LocalIntents::Reply clockReply(const String &text) {
  return {text, LocalIntents::Action::Clock};
}

String alarmTime(time_t when) {
  struct tm local{};
  char text[40]{};
  if (localtime_r(&when, &local)) strftime(text, sizeof(text), "%A at %I:%M %p", &local);
  return text;
}

// Handle wake words and conversational phrasing before considering a cloud reply.
String commandBody(String text) {
  const char *prefixes[] = {"hey sam ", "hey ", "okay sam ", "ok sam ", "sam ", "please ",
                            "can you ", "could you ", "would you ", "will you ", "like "};
  for (unsigned pass = 0; pass < 4; ++pass) {
    bool stripped = false;
    for (const char *prefix : prefixes) {
      if (!text.startsWith(prefix)) continue;
      text.remove(0, strlen(prefix));
      stripped = true;
      break;
    }
    if (!stripped) break;
  }
  if (text.endsWith(" please")) text.remove(text.length()-7);
  return text;
}

bool alarmRequest(const String &command, String &argument) {
  const char *prefixes[] = {"set an alarm", "set a alarm", "set the alarm", "set my alarm",
                            "set alarm", "put an alarm", "put a alarm", "put the alarm", "put my alarm", "put alarm",
                            "create an alarm", "add an alarm", "wake me up", "wake me"};
  for (const char *prefix : prefixes) {
    if (command == prefix) return true;
    if (!command.startsWith(String(prefix) + " ")) continue;
    argument = command.substring(strlen(prefix)+1);
    if (argument.startsWith("for me ")) argument.remove(0,7);
    return true;
  }
  return false;
}

unsigned durationSeconds(const String &argument) {
  const int split = argument.lastIndexOf(' ');
  if (split <= 0) return 0;
  const auto unit = argument.substring(split+1);
  unsigned scale;
  if (unit == "second" || unit == "seconds" || unit == "sec") scale = 1;
  else if (unit == "minute" || unit == "minutes" || unit == "min") scale = 60;
  else if (unit == "hour" || unit == "hours" || unit == "hr") scale = 3600;
  else return 0;
  unsigned amount;
  auto value = argument.substring(0,split);
  if (value == "a" || value == "an") amount = 1;
  else if (!number(value, amount)) return 0;
  return amount && amount <= 86400/scale ? amount*scale : 0;
}

bool clockFields(String clock, unsigned &hour, unsigned &minute, bool &ambiguous) {
  clock.replace("a m", "am");
  clock.replace("p m", "pm");
  const bool am = clock.endsWith("am"), pm = clock.endsWith("pm");
  if (am || pm) clock.remove(clock.length()-2);
  clock.trim();
  if (clock.endsWith(" o clock")) clock.remove(clock.length()-8);

  minute = 0;
  const bool explicit24h = clock.length() >= 3 && clock[0] == '0' && clock.indexOf(':') == 2;
  const int past = clock.indexOf(" past ");
  const int to = clock.indexOf(" to ");
  const int colon = clock.indexOf(':');
  if (past > 0 || to > 0) {
    const bool before = to > 0;
    const int split = before ? to : past;
    auto first = clock.substring(0,split);
    if (first == "quarter" || first == "a quarter") minute = 15;
    else if (first == "half" || first == "a half") minute = 30;
    else if (!number(first, minute)) return false;
    if (!minute || minute >= 60 || !number(clock.substring(split+(before ? 4 : 6)), hour)) return false;
    if (before) {
      if (hour < 1 || hour > 12 || am || pm) return false; // Resolve the target-hour period in the bridge.
      hour = hour == 1 ? 12 : hour-1;
      minute = 60-minute;
    }
  } else if (colon >= 0) {
    if (!digits(clock.substring(0,colon), hour) || !digits(clock.substring(colon+1), minute)) return false;
  } else if (!number(clock, hour)) {
    bool parsed = false;
    for (int split = clock.indexOf(' '); split >= 0; split = clock.indexOf(' ', split+1)) {
      auto spokenMinute = clock.substring(split+1);
      if (spokenMinute.startsWith("oh ")) spokenMinute.remove(0,3);
      if (number(clock.substring(0,split), hour) && number(spokenMinute, minute) && minute < 60) {
        parsed = true;
        break;
      }
    }
    if (!parsed) return false;
  }
  if (minute >= 60) return false;
  if (am || pm) {
    if (hour < 1 || hour > 12) return false;
    hour = hour % 12 + (pm ? 12 : 0);
  }
  ambiguous = !am && !pm && !explicit24h && hour >= 1 && hour <= 12;
  return hour < 24;
}

Reply timeReply(time_t now, String text = {}, bool date = false) {
  struct tm local{};
  char value[64];
  if (!Alarms::clockReady(now) || !localtime_r(&now, &local)) return clockReply("My clock is waiting to sync.");
  strftime(value, sizeof(value), date ? "%A, %B %d, %Y" : "%I:%M %p", &local);
  if (value[0] == '0') memmove(value, value + 1, strlen(value));
  if (!text.length()) text = "It is {value}.";
  text.replace("{value}", value);
  return clockReply(text);
}

const char *dayLabel(AlarmDay day) {
  return day == AlarmDay::Tomorrow ? " tomorrow" : day == AlarmDay::Today ? " today" : "";
}

Reply finishAlarm(const Intent &intent, time_t now) {
  if (!Alarms::clockReady(now)) return clockReply("My clock is waiting to sync.");
  if (!intent.delay && (intent.hour < 0 || intent.ambiguous)) {
    pendingAlarm = intent;
    pendingAt = millis();
    if (intent.hour < 0) return clockReply(String("What time") + dayLabel(intent.day) + "?");
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "Did you mean %d:%02d A M or P M%s?", intent.hour, intent.minute, dayLabel(intent.day));
    return clockReply(prompt);
  }
  clearPendingAlarm();
  const time_t deadline = intent.delay ? now + intent.delay :
      Alarms::nextTime(now, intent.hour, intent.minute, intent.day == AlarmDay::Tomorrow);
  if (!deadline) return clockReply("I couldn't resolve that alarm time.");
  if (intent.day == AlarmDay::Today) {
    struct tm today{}, scheduled{};
    if (!localtime_r(&now, &today) || !localtime_r(&deadline, &scheduled) ||
        today.tm_yday != scheduled.tm_yday || today.tm_year != scheduled.tm_year)
      return clockReply("That time has passed today. Ask for tomorrow or choose a later time.");
  }
  const auto result = Alarms::save(Alarms::NoSlot, deadline);
  if (result == Alarms::Result::Full) return clockReply("The alarm list is full. Delete an alarm to make room.");
  if (result != Alarms::Result::Saved) return clockReply("I couldn't save that alarm.");
  return clockReply(String("Alarm set on the Pod for ") + alarmTime(deadline) + ".");
}

Reply parseAlarm(String argument, time_t now, AlarmDay defaultDay = AlarmDay::Next, bool followUp = false) {
  Intent intent;
  intent.kind = Interpretation::Alarm;
  intent.day = defaultDay;
  if (argument.startsWith("for ")) argument.remove(0,4);
  const char *dayWords[] = {"today", "tomorrow"};
  for (unsigned i = 0; i < 2; ++i) {
    const String day = dayWords[i];
    if (argument == day) argument = "";
    else if (argument.startsWith(day + " ")) argument.remove(0,day.length()+1);
    else if (argument.endsWith(String(" ") + day)) argument.remove(argument.length()-day.length()-1);
    else continue;
    intent.day = i == 0 ? AlarmDay::Today : AlarmDay::Tomorrow;
    break;
  }
  bool relative = false;
  if (argument.startsWith("in ")) { argument.remove(0,3); relative = true; }
  if (argument.endsWith(" from now")) { argument.remove(argument.length()-9); relative = true; }
  if (argument.startsWith("at ")) argument.remove(0,3);
  bool tonight = argument.endsWith(" tonight");
  if (tonight) {
    intent.day = AlarmDay::Today;
    argument.remove(argument.length()-8);
  }
  if (!argument.length() && !followUp) return finishAlarm(intent, now);
  if (intent.day == AlarmDay::Next || relative) intent.delay = durationSeconds(argument);
  unsigned hour, minute;
  if (intent.delay) {
    intent.day = AlarmDay::Next;
    return finishAlarm(intent, now);
  }
  if (!relative && clockFields(argument, hour, minute, intent.ambiguous)) {
    if (tonight && intent.ambiguous) { hour = hour % 12 + 12; intent.ambiguous = false; }
    intent.hour = hour;
    intent.minute = minute;
    return finishAlarm(intent, now);
  }
  if (followUp) return {};
  return {"Say a time like seven A M, or a duration like five minutes.", Action::Interpret};
}

Reply alarmFollowUp(const String &command, time_t now) {
  if (pendingAlarm.kind != Interpretation::Alarm) return {};
  if (uint32_t(millis() - pendingAt) >= 90000) { clearPendingAlarm(); return {}; }
  if (command == "never mind" || command == "nevermind" || command == "cancel that" || command == "no") {
    clearPendingAlarm();
    return clockReply("Okay, I left your alarms as they were.");
  }
  const bool am = command == "am" || command == "a m" || command == "in the morning" || command == "morning";
  const bool pm = command == "pm" || command == "p m" || command == "in the evening" ||
                  command == "in the afternoon" || command == "evening" || command == "afternoon";
  if (pendingAlarm.ambiguous && (am || pm)) {
    auto resolved = pendingAlarm;
    resolved.hour = resolved.hour % 12 + (pm ? 12 : 0);
    resolved.ambiguous = false;
    return finishAlarm(resolved, now);
  }
  return parseAlarm(command, now, pendingAlarm.day, true);
}

}

namespace LocalIntents {
void clearPendingAlarm() { pendingAlarm = {}; }

Reply apply(const Intent &intent) {
  const auto now = time(nullptr);
  if (intent.kind == Interpretation::Clock) return timeReply(now);
  if (intent.kind != Interpretation::Alarm || intent.delay < 0 || intent.delay > 86400 ||
      intent.hour < -1 || intent.hour > 23 || intent.minute < 0 || intent.minute > 59 ||
      (intent.ambiguous && (intent.hour < 1 || intent.hour > 12)) ||
      (intent.delay && (intent.hour >= 0 || intent.day != AlarmDay::Next))) return {};
  return finishAlarm(intent, now);
}

void begin() {
  auto file = SD_MMC.open("/assistant/intents.json", FILE_READ);
  if (!file || file.isDirectory() || file.size() > 12288) return;
  if (deserializeJson(intents, file, DeserializationOption::NestingLimit(7))) {
    intents.clear();
    Serial.println("Assistant: invalid /assistant/intents.json");
  } else {
    Serial.println("Assistant: loaded SD intents");
  }
}

Reply match(const char *transcript, int volume, bool musicOnline) {
  const String normalized = normalize(transcript);
  const String localCommand = commandBody(normalized);
  const auto now = time(nullptr);
  String alarmArgument;
  if (alarmRequest(localCommand, alarmArgument)) {
    clearPendingAlarm();
    return parseAlarm(alarmArgument, now);
  }
  const auto followUp = alarmFollowUp(localCommand, now);
  if (followUp.action != Action::None) return followUp;
  const String &command = normalized;
  const char *action = "";
  String text;
  for (JsonObjectConst rule : intents["intents"].as<JsonArrayConst>()) {
    for (const char *phrase : rule["phrases"].as<JsonArrayConst>()) {
      if (phrase && command == normalize(phrase)) {
        action = rule["action"] | "";
        text = variant(rule["responses"].as<JsonArrayConst>());
        break;
      }
    }
    if (*action) break;
  }
  const bool askTime = localCommand == "time" || localCommand == "now time" ||
                       localCommand == "current time" || localCommand == "time now" ||
                       localCommand == "what time" || localCommand == "what time now" ||
                       (localCommand == "what time is it" || localCommand == "what time is it now" ||
                        localCommand == "what time is it right now" || localCommand == "what time is it on the pod") ||
                       (localCommand == "what is the time" || localCommand == "what is the time now") ||
                       (localCommand == "whats the time" || localCommand == "whats the time now" ||
                        localCommand == "what s the time" || localCommand == "what s the time now") ||
                       (localCommand == "tell me the time" || localCommand == "tell me the time now") || localCommand == "tell me current time" ||
                       localCommand == "give me the current time" || localCommand == "do you know what time it is";
  if (askTime) return timeReply(now, !strcmp(action, "time") ? text : String());
  if (!strcmp(action, "time") || !strcmp(action, "date")) return timeReply(now, text, !strcmp(action, "date"));
  if (!strcmp(action, "status")) {
    text.replace("{volume}", String(volume));
    text.replace("{music}", musicOnline ? "connected" : "disconnected");
    return {text};
  }
  if (!strcmp(action, "track")) {
    auto playing = Music::snapshot();
    if (!playing.online || !playing.title[0]) return {"There is no desktop track to report."};
    String answer = playing.title;
    if (playing.artist[0]) answer += String(" by ") + playing.artist;
    return {answer + (playing.trackState == 1 ? " is playing." : " is the current track.")};
  }
  if (!strcmp(action, "volume")) {
    text.replace("{volume}", String(volume));
    return {text};
  }
  int target = volume;
  unsigned amount = 0;
  if (command.startsWith("set volume to ")) {
    String argument = command.substring(14);
    if (argument.endsWith(" percent")) argument.remove(argument.length() - 8);
    else if (argument.endsWith("%")) argument.remove(argument.length() - 1);
    if (!number(argument, amount) || amount > 100) return {"Choose a volume from zero to one hundred percent."};
    target = amount;
  } else if (command == "volume up") target = min(volume + 10, 100);
  else if (command == "volume down") target = max(volume - 10, 0);
  else if (command == "mute") target = 0;
  else if (localCommand == "cancel alarm" || localCommand == "cancel my alarm" ||
           localCommand == "cancel all alarms" || localCommand == "cancel the next alarm") {
    const bool all = localCommand == "cancel all alarms";
    const auto result = Alarms::cancel(all);
    if (result == Alarms::Result::Missing) return clockReply("There's no upcoming alarm.");
    if (result != Alarms::Result::Saved) return clockReply("I couldn't save that change.");
    return clockReply(all ? "All alarms are turned off." : "Your next alarm is turned off.");
  } else if (localCommand == "what is my alarm" || localCommand == "alarm status" ||
             localCommand == "when is my next alarm" || localCommand == "what is my next alarm") {
    if (!Alarms::clockReady(now)) return clockReply("My clock is waiting to sync.");
    const time_t deadline = Alarms::next(Alarms::snapshot(), now);
    return clockReply(deadline ? String("Your next alarm is ") + alarmTime(deadline) + "."
                               : String("There's no upcoming alarm."));
  } else {
    Music::Command music;
    if (!strcmp(action, "music") || Music::phraseCommand(command.c_str(), music) ||
        command.startsWith("play "))
      return {text.length() ? text : String("Music request queued."), Action::Music};
    if (!strcmp(action, "reply")) return {text};
    const String words = String(" ") + localCommand + " ";
    if (words.indexOf(" alarm ") >= 0 || words.indexOf(" wake ") >= 0 ||
        words.indexOf(" time ") >= 0 || words.indexOf(" clock ") >= 0) {
      return {"", Action::Interpret};
    }
    return {};
  }
  return {String("Pod volume set to ") + target + " percent.", Action::Volume, target};
}
}

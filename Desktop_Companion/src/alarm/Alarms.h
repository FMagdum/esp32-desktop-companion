#pragma once

#include <stdint.h>
#include <time.h>

namespace Alarms {
constexpr int Capacity = 8;
constexpr int NoSlot = -1;
struct Entry {
  uint32_t when = 0;
  uint32_t enabled = 0;
};
struct Snapshot {
  Entry entries[Capacity];
  time_t ringing = 0;
  uint32_t revision = 0;
};
enum class Result { Saved, ClockRequired, Full, Missing, StorageError };

void begin();
bool startScheduler();
bool isRinging();
bool hasArmedAlarm();
bool clockReady(time_t now);
time_t nextTime(time_t now, int hour, int minute, bool tomorrow = false);
Snapshot snapshot();
time_t next(const Snapshot &state, time_t now);
Result save(int slot, time_t when);
Result toggle(int slot);
Result remove(int slot);
Result cancel(bool all);
void update(time_t now, uint32_t uptime);
void stop();
Result snooze();
}

#include "Alarms.h"

#include <Arduino.h>
#include <Preferences.h>
#include <mutex>
#include <atomic>
#include <freertos/task.h>

namespace {
using namespace Alarms;
using Entries = Entry[Capacity];
constexpr char Key[] = "schedule1";
constexpr uint32_t CatchUpSeconds = 300;
constexpr uint32_t RingDurationMs = 120000;
constexpr uint32_t SnoozeSeconds = 300;
std::mutex lock;
Preferences preferences;
bool writable = false;
Snapshot state;
std::atomic<bool> ringingNow{false};
int ringingSlot = NoSlot;
uint32_t ringStarted = 0;

bool store(const Entries &entries) {
  return writable && preferences.putBytes(Key, entries, sizeof(entries)) == sizeof(entries);
}

Result commit(const Entries &entries) {
  if (!store(entries)) return Result::StorageError;
  memcpy(state.entries, entries, sizeof(entries));
  ++state.revision;
  return Result::Saved;
}

bool occupied(int slot) {
  return slot >= 0 && slot < Capacity && state.entries[slot].when && slot != ringingSlot;
}

int earliest(time_t after) {
  int found = NoSlot;
  for (int i = 0; i < Capacity; ++i) {
    const auto &entry = state.entries[i];
    if (entry.enabled && entry.when > after &&
        (found == NoSlot || entry.when < state.entries[found].when)) found = i;
  }
  return found;
}

void clearRing() {
  ringingSlot = NoSlot;
  state.ringing = 0;
  ringingNow.store(false);
  ++state.revision;
}

bool validDeadline(time_t when, time_t now) {
  return clockReady(now) && when > now && uint64_t(when) <= UINT32_MAX;
}
}

namespace Alarms {
void begin() {
  std::lock_guard<std::mutex> guard(lock);
  writable = preferences.begin("pod-alarm", false);
  if (!writable) {
    Serial.println("Alarms: flash storage unavailable");
    return;
  }
  const size_t length = preferences.getBytesLength(Key);
  if (length == sizeof(state.entries)) {
    preferences.getBytes(Key, state.entries, sizeof(state.entries));
  } else if (!length) {
    // The original voice alarm prototype stored eight enabled timestamps.
    uint32_t legacy[Capacity]{};
    if (preferences.getBytesLength("alarms") == sizeof(legacy) &&
        preferences.getBytes("alarms", legacy, sizeof(legacy)) == sizeof(legacy)) {
      for (int i = 0; i < Capacity; ++i) state.entries[i] = {legacy[i], legacy[i] != 0};
      if (!store(state.entries)) Serial.println("Alarms: could not migrate saved voice alarms");
    }
  }
}

bool startScheduler() {
  return xTaskCreate([](void *) {
    for (;;) {
      update(time(nullptr), millis());
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }, "alarm-clock", 3072, nullptr, 2, nullptr) == pdPASS;
}

bool isRinging() { return ringingNow.load(); }

bool hasArmedAlarm() {
  std::lock_guard<std::mutex> guard(lock);
  return state.ringing || earliest(0) != NoSlot;
}

bool clockReady(time_t now) { return now >= 1704067200; }

time_t nextTime(time_t now, int hour, int minute, bool tomorrow) {
  struct tm local{};
  if (!clockReady(now) || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      !localtime_r(&now, &local)) return 0;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  if (tomorrow) ++local.tm_mday;
  time_t deadline = mktime(&local);
  if (!tomorrow && deadline <= now) {
    ++local.tm_mday;
    deadline = mktime(&local);
  }
  return deadline > now ? deadline : 0;
}

Snapshot snapshot() {
  std::lock_guard<std::mutex> guard(lock);
  return state;
}

time_t next(const Snapshot &value, time_t now) {
  time_t deadline = 0;
  for (const auto &entry : value.entries)
    if (entry.enabled && entry.when > now && (!deadline || entry.when < deadline)) deadline = entry.when;
  return deadline;
}

Result save(int slot, time_t when) {
  std::lock_guard<std::mutex> guard(lock);
  if (!validDeadline(when, time(nullptr))) return Result::ClockRequired;
  if (slot == NoSlot) {
    for (int i = 0; i < Capacity; ++i) {
      if (state.entries[i].enabled && state.entries[i].when == when) return Result::Saved;
      if (slot == NoSlot && !state.entries[i].when) slot = i;
    }
    if (slot == NoSlot) return Result::Full;
  } else if (!occupied(slot)) {
    return Result::Missing;
  }
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  updated[slot] = {uint32_t(when), 1};
  const auto result = commit(updated);
  if (result == Result::Saved) {
    struct tm local{};
    char date[32]{};
    if (localtime_r(&when, &local)) strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local);
    Serial.printf("Alarms: scheduled %s local (slot %d)\n", date, slot + 1);
  }
  return result;
}

Result toggle(int slot) {
  std::lock_guard<std::mutex> guard(lock);
  if (!occupied(slot)) return Result::Missing;
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  auto &entry = updated[slot];
  if (!entry.enabled) {
    const time_t now = time(nullptr);
    time_t deadline = entry.when;
    if (deadline <= now) {
      struct tm local{};
      if (!localtime_r(&deadline, &local)) return Result::ClockRequired;
      deadline = nextTime(now, local.tm_hour, local.tm_min);
    }
    if (!validDeadline(deadline, now)) return Result::ClockRequired;
    entry.when = uint32_t(deadline);
  }
  entry.enabled = !entry.enabled;
  return commit(updated);
}

Result remove(int slot) {
  std::lock_guard<std::mutex> guard(lock);
  if (!occupied(slot)) return Result::Missing;
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  updated[slot] = {};
  return commit(updated);
}

Result cancel(bool all) {
  std::lock_guard<std::mutex> guard(lock);
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  if (all) {
    for (auto &entry : updated) entry.enabled = 0;
  } else {
    const int slot = earliest(0);
    if (slot == NoSlot) return Result::Missing;
    updated[slot].enabled = 0;
  }
  return commit(updated);
}

void update(time_t now, uint32_t uptime) {
  std::lock_guard<std::mutex> guard(lock);
  if (state.ringing && uint32_t(uptime - ringStarted) >= RingDurationMs) clearRing();
  if (state.ringing || !clockReady(now)) return;

  const int slot = earliest(0);
  if (slot == NoSlot || state.entries[slot].when > now) return;
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  for (auto &entry : updated) {
    if (entry.enabled && time_t(entry.when) <= now && now - time_t(entry.when) > CatchUpSeconds) {
      entry.enabled = 0;
      Serial.println("Alarms: missed alarm outside the catch-up window");
    }
  }

  int due = NoSlot;
  for (int i = 0; i < Capacity; ++i) {
    const auto &entry = updated[i];
    if (entry.enabled && entry.when <= now &&
        (due == NoSlot || entry.when < updated[due].when)) due = i;
  }
  if (due != NoSlot) updated[due].enabled = 0;
  if (commit(updated) != Result::Saved) {
    // Still ring once this boot if flash becomes unwritable.
    memcpy(state.entries, updated, sizeof(updated));
    ++state.revision;
    Serial.println("Alarms: could not persist due alarms");
  }
  if (due != NoSlot) {
    ringingSlot = due;
    state.ringing = updated[due].when;
    ringStarted = uptime;
    ringingNow.store(true);
    Serial.println("Alarms: ringing");
  }
}

void stop() {
  std::lock_guard<std::mutex> guard(lock);
  if (!state.ringing) return;
  clearRing();
  Serial.println("Alarms: stopped");
}

Result snooze() {
  std::lock_guard<std::mutex> guard(lock);
  if (!state.ringing) return Result::Missing;
  const time_t now = time(nullptr);
  const time_t deadline = now + SnoozeSeconds;
  if (!validDeadline(deadline, now)) return Result::ClockRequired;
  Entries updated;
  memcpy(updated, state.entries, sizeof(updated));
  updated[ringingSlot] = {uint32_t(deadline), 1};
  const auto result = commit(updated);
  if (result == Result::Saved) {
    clearRing();
    Serial.println("Alarms: snoozed for five minutes");
  }
  return result;
}
}

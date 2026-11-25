#pragma once
#include <stdint.h>
#include "DeviceSettings.h"
namespace RemoteSettings {
struct Command {
  uint32_t id = 0;
  enum Action : uint8_t { None, Settings, Enroll, Test, Sleep } action = None;
  DeviceSettings::Values values;
};
struct Status {
  DeviceSettings::Values values;
  uint32_t ack = 0;
  char result[96] = "Pod connected";
  bool musicOnline = false, wakeTrained = false, teaching = false, sdAvailable = false;
  char wakeTest[40] = "";
  uint64_t sdTotal = 0, sdUsed = 0;
  uint8_t flashMb = 0, psramMb = 0;
};
}


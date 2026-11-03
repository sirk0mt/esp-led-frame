// On/off schedule: switch the LEDs on or off at set times on chosen weekdays.
//
// The schedule is part of the configuration (/config.json, GET/POST /api/config).
// ScheduleRunner is a pure class: App feeds it the wall time about once per
// second, and it reports an action once, in the minute an entry's time is reached.
// Nothing happens while the time is unknown, and there is no catching up: an entry
// whose minute passed while the device was off or without time does not fire later.
// Minutes skipped by a clock jump (the spring change to summer time) are skipped too,
// and minutes repeated by a jump back may fire again (the same action twice).
#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "core/WallTime.h"

namespace ledframe {

struct ScheduleEntry {
  static constexpr uint8_t kEveryDay = 0x7F;

  bool enabled = true;
  uint8_t days = kEveryDay;  // bit 0 = Monday .. bit 6 = Sunday (WallTime::weekday)
  uint8_t hour = 0;          // 0..23, local time
  uint8_t minute = 0;        // 0..59
  bool turnOn = false;       // true: switch on; false: switch off
};

struct Schedule {
  static constexpr uint8_t kMaxEntries = 8;

  ScheduleEntry entries[kMaxEntries];
  uint8_t count = 0;
};

bool operator==(const ScheduleEntry& a, const ScheduleEntry& b);
bool operator==(const Schedule& a, const Schedule& b);
inline bool operator!=(const Schedule& a, const Schedule& b) {
  return !(a == b);
}

class ScheduleRunner {
 public:
  enum class Action : uint8_t { kNone, kOn, kOff };

  // Call often (App: once per second). Returns kOn or kOff once per matching minute:
  // the action of the last enabled entry for this weekday, hour and minute.
  Action update(const Schedule& schedule, const WallTime& now);

 private:
  int32_t lastMinute_ = -1;  // weekday * 1440 + minute of day of the last checked minute
};

// JSON form, used by /config.json and the API:
//   [{"enabled": true, "days": [0, 1, 2, 3, 4], "time": "07:30", "action": "on"}, ...]
// days: weekday numbers, 0 = Monday .. 6 = Sunday; time: "HH:MM", 24 h; action: "on"
// or "off"; enabled is optional (true).
void scheduleToJson(const Schedule& schedule, JsonArray out);

// Reads the JSON form into out, all or nothing. Returns false with a static message
// in error for anything that is not valid (not an array, more than kMaxEntries
// entries, a bad time, an empty or out of range day list, an unknown action); out is
// then unchanged.
bool scheduleFromJson(JsonVariantConst in, Schedule& out, const char*& error);

}  // namespace ledframe

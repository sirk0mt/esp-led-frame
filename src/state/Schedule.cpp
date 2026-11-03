#include "state/Schedule.h"

#include <cstdio>
#include <cstring>

namespace ledframe {

namespace {

// "HH:MM" with two digits each, 00:00..23:59.
bool parseTime(const char* text, uint8_t& hour, uint8_t& minute) {
  if (text == nullptr || std::strlen(text) != 5 || text[2] != ':') {
    return false;
  }
  for (int i : {0, 1, 3, 4}) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  const int h = (text[0] - '0') * 10 + (text[1] - '0');
  const int m = (text[3] - '0') * 10 + (text[4] - '0');
  if (h > 23 || m > 59) {
    return false;
  }
  hour = static_cast<uint8_t>(h);
  minute = static_cast<uint8_t>(m);
  return true;
}

}  // namespace

bool operator==(const ScheduleEntry& a, const ScheduleEntry& b) {
  return a.enabled == b.enabled && a.days == b.days && a.hour == b.hour && a.minute == b.minute &&
         a.turnOn == b.turnOn;
}

bool operator==(const Schedule& a, const Schedule& b) {
  if (a.count != b.count) {
    return false;
  }
  for (uint8_t i = 0; i < a.count; ++i) {
    if (!(a.entries[i] == b.entries[i])) {
      return false;
    }
  }
  return true;
}

ScheduleRunner::Action ScheduleRunner::update(const Schedule& schedule, const WallTime& now) {
  if (!now.known || now.weekday > 6) {
    return Action::kNone;
  }
  const int32_t minute = now.weekday * 1440 + now.minuteOfDay();
  if (minute == lastMinute_) {
    return Action::kNone;  // This minute was handled already.
  }
  lastMinute_ = minute;
  Action action = Action::kNone;
  for (uint8_t i = 0; i < schedule.count && i < Schedule::kMaxEntries; ++i) {
    const ScheduleEntry& entry = schedule.entries[i];
    if (entry.enabled && ((entry.days >> now.weekday) & 1u) != 0 && entry.hour == now.hour &&
        entry.minute == now.minute) {
      action = entry.turnOn ? Action::kOn : Action::kOff;  // the last matching entry wins
    }
  }
  return action;
}

void scheduleToJson(const Schedule& schedule, JsonArray out) {
  for (uint8_t i = 0; i < schedule.count && i < Schedule::kMaxEntries; ++i) {
    const ScheduleEntry& entry = schedule.entries[i];
    JsonObject item = out.add<JsonObject>();
    item["enabled"] = entry.enabled;
    JsonArray days = item["days"].to<JsonArray>();
    for (uint8_t day = 0; day < 7; ++day) {
      if ((entry.days >> day) & 1u) {
        days.add(day);
      }
    }
    char time[8];
    std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(entry.hour % 24),
                  static_cast<unsigned>(entry.minute % 60));
    item["time"] = time;
    item["action"] = entry.turnOn ? "on" : "off";
  }
}

bool scheduleFromJson(JsonVariantConst in, Schedule& out, const char*& error) {
  if (!in.is<JsonArrayConst>()) {
    error = "schedule must be an array";
    return false;
  }
  const JsonArrayConst list = in.as<JsonArrayConst>();
  if (list.size() > Schedule::kMaxEntries) {
    error = "schedule: at most 8 entries";
    return false;
  }
  Schedule next;
  for (JsonVariantConst value : list) {
    if (!value.is<JsonObjectConst>()) {
      error = "schedule entries must be objects";
      return false;
    }
    const JsonObjectConst item = value.as<JsonObjectConst>();
    ScheduleEntry entry;
    const JsonVariantConst enabled = item["enabled"];
    if (!enabled.isNull()) {
      if (!enabled.is<bool>()) {
        error = "schedule enabled must be true or false";
        return false;
      }
      entry.enabled = enabled.as<bool>();
    }
    if (!item["time"].is<const char*>() ||
        !parseTime(item["time"].as<const char*>(), entry.hour, entry.minute)) {
      error = "schedule time must be HH:MM, 00:00-23:59";
      return false;
    }
    const char* action = item["action"].is<const char*>() ? item["action"].as<const char*>() : "";
    if (std::strcmp(action, "on") == 0) {
      entry.turnOn = true;
    } else if (std::strcmp(action, "off") == 0) {
      entry.turnOn = false;
    } else {
      error = "schedule action must be on or off";
      return false;
    }
    if (!item["days"].is<JsonArrayConst>()) {
      error = "schedule days must be a list of 0-6 (Monday 0)";
      return false;
    }
    entry.days = 0;
    for (JsonVariantConst day : item["days"].as<JsonArrayConst>()) {
      if (!day.is<int>() || day.as<int>() < 0 || day.as<int>() > 6) {
        error = "schedule days must be a list of 0-6 (Monday 0)";
        return false;
      }
      entry.days = static_cast<uint8_t>(entry.days | (1u << day.as<int>()));
    }
    if (entry.days == 0) {
      error = "schedule days must not be empty";
      return false;
    }
    next.entries[next.count++] = entry;
  }
  out = next;
  return true;
}

}  // namespace ledframe

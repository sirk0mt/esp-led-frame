#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>

#include "state/Config.h"
#include "state/Schedule.h"

using ledframe::Config;
using ledframe::Schedule;
using ledframe::ScheduleEntry;
using ledframe::ScheduleRunner;
using ledframe::WallTime;
using Action = ledframe::ScheduleRunner::Action;

namespace {

constexpr uint8_t kMonday = 0;
constexpr uint8_t kSaturday = 5;
constexpr uint8_t kWeekdays = 0x1F;  // Monday..Friday

WallTime at(uint8_t weekday, uint8_t hour, uint8_t minute, uint8_t second = 0) {
  WallTime time;
  time.known = true;
  time.year = 2026;
  time.month = 11;
  time.day = 2;
  time.weekday = weekday;
  time.hour = hour;
  time.minute = minute;
  time.second = second;
  return time;
}

ScheduleEntry entry(uint8_t hour, uint8_t minute, bool turnOn, uint8_t days = 0x7F) {
  ScheduleEntry e;
  e.hour = hour;
  e.minute = minute;
  e.turnOn = turnOn;
  e.days = days;
  return e;
}

// On at 07:30 on weekdays, off at 23:00 every day.
Schedule sample() {
  Schedule schedule;
  schedule.entries[0] = entry(7, 30, true, kWeekdays);
  schedule.entries[1] = entry(23, 0, false);
  schedule.count = 2;
  return schedule;
}

bool fromJson(const char* json, Schedule& out, const char*& error) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  return ledframe::scheduleFromJson(doc.as<JsonVariantConst>(), out, error);
}

}  // namespace

void setUp() {}
void tearDown() {}

// An entry fires once, in its minute: not before, not again within the minute.
void test_entry_fires_once_in_its_minute() {
  ScheduleRunner runner;
  const Schedule schedule = sample();
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 29, 59)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 30, 0)) == Action::kOn);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 30, 1)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 30, 59)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 31, 0)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 23, 0, 3)) == Action::kOff);
}

// The weekday list counts: the 07:30 entry is for weekdays only.
void test_entry_only_on_its_days() {
  ScheduleRunner runner;
  const Schedule schedule = sample();
  TEST_ASSERT_TRUE(runner.update(schedule, at(kSaturday, 7, 30)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kSaturday, 23, 0)) == Action::kOff);
  TEST_ASSERT_TRUE(runner.update(schedule, at(6, 23, 0)) == Action::kOff);  // Sunday
}

// The same minute on another day fires again.
void test_next_day_fires_again() {
  ScheduleRunner runner;
  const Schedule schedule = sample();
  TEST_ASSERT_TRUE(runner.update(schedule, at(1, 23, 0)) == Action::kOff);
  TEST_ASSERT_TRUE(runner.update(schedule, at(2, 23, 0)) == Action::kOff);
}

// Unknown time: nothing happens, and the first known minute is still checked.
void test_unknown_time_does_nothing() {
  ScheduleRunner runner;
  const Schedule schedule = sample();
  WallTime unknown;
  TEST_ASSERT_TRUE(runner.update(schedule, unknown) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 30, 20)) == Action::kOn);
}

// Disabled entries are kept but never fire; with two entries in one minute the last
// one wins.
void test_disabled_and_last_wins() {
  Schedule schedule;
  schedule.entries[0] = entry(6, 0, true);
  schedule.entries[0].enabled = false;
  schedule.entries[1] = entry(20, 15, true);
  schedule.entries[2] = entry(20, 15, false);
  schedule.count = 3;
  ScheduleRunner runner;
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 6, 0)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 20, 15)) == Action::kOff);
}

// A missed minute does not fire later (no catching up).
void test_no_catching_up() {
  ScheduleRunner runner;
  const Schedule schedule = sample();
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 29)) == Action::kNone);
  TEST_ASSERT_TRUE(runner.update(schedule, at(kMonday, 7, 32)) == Action::kNone);
}

void test_json_round_trip() {
  JsonDocument doc;
  ledframe::scheduleToJson(sample(), doc.to<JsonArray>());
  char text[256];
  serializeJson(doc, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"enabled\":true,\"days\":[0,1,2,3,4],\"time\":\"07:30\",\"action\":\"on\"},"
      "{\"enabled\":true,\"days\":[0,1,2,3,4,5,6],\"time\":\"23:00\",\"action\":\"off\"}]",
      text);
  Schedule out;
  const char* error = "";
  TEST_ASSERT_TRUE(fromJson(text, out, error));
  TEST_ASSERT_TRUE(out == sample());
}

// enabled is optional; anything else wrong rejects the whole list and keeps out.
void test_json_validation() {
  Schedule out = sample();
  const char* error = "";
  TEST_ASSERT_TRUE(fromJson("[{\"days\":[6],\"time\":\"09:05\",\"action\":\"on\"}]", out, error));
  TEST_ASSERT_EQUAL_UINT8(1, out.count);
  TEST_ASSERT_TRUE(out.entries[0].enabled);
  TEST_ASSERT_EQUAL_UINT8(0x40, out.entries[0].days);
  TEST_ASSERT_TRUE(fromJson("[]", out, error));
  TEST_ASSERT_EQUAL_UINT8(0, out.count);

  out = sample();
  const char* const kBad[] = {
      "{}",                                                     // not an array
      "[1]",                                                    // not an object
      "[{\"days\":[0],\"time\":\"24:00\",\"action\":\"on\"}]",  // hour
      "[{\"days\":[0],\"time\":\"7:30\",\"action\":\"on\"}]",   // one digit
      "[{\"days\":[0],\"time\":\"07:60\",\"action\":\"on\"}]",  // minute
      "[{\"days\":[0],\"action\":\"on\"}]",                     // no time
      "[{\"days\":[],\"time\":\"07:30\",\"action\":\"on\"}]",   // no days
      "[{\"days\":[7],\"time\":\"07:30\",\"action\":\"on\"}]",  // day out of range
      "[{\"days\":\"mon\",\"time\":\"07:30\",\"action\":\"on\"}]",
      "[{\"days\":[0],\"time\":\"07:30\",\"action\":\"toggle\"}]",
      "[{\"days\":[0],\"time\":\"07:30\",\"action\":\"on\",\"enabled\":1}]",
  };
  for (const char* json : kBad) {
    error = "";
    TEST_ASSERT_FALSE_MESSAGE(fromJson(json, out, error), json);
    TEST_ASSERT_TRUE(std::strlen(error) > 0);
    TEST_ASSERT_TRUE(out == sample());  // unchanged
  }
  char nine[512] = "[";
  for (int i = 0; i < 9; ++i) {
    std::strcat(nine, i == 0 ? "" : ",");
    std::strcat(nine, "{\"days\":[0],\"time\":\"07:30\",\"action\":\"on\"}");
  }
  std::strcat(nine, "]");
  TEST_ASSERT_FALSE(fromJson(nine, out, error));
  TEST_ASSERT_NOT_NULL(std::strstr(error, "8"));
}

// The schedule is part of /config.json; a broken one in the file gives no schedule.
void test_config_keeps_the_schedule() {
  Config config;
  config.schedule = sample();
  char buffer[1024];
  const size_t length = ledframe::serializeConfig(config, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);
  Config out;
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(buffer, length, out));
  TEST_ASSERT_TRUE(out.schedule == sample());
  TEST_ASSERT_TRUE(out == config);

  const char kBroken[] = "{\"schedule\":[{\"time\":\"99:99\"}],\"fps\":60}";
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(kBroken, std::strlen(kBroken), out));
  TEST_ASSERT_EQUAL_UINT8(0, out.schedule.count);
  TEST_ASSERT_EQUAL_UINT8(60, out.fps);  // the rest still reads
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_entry_fires_once_in_its_minute);
  RUN_TEST(test_entry_only_on_its_days);
  RUN_TEST(test_next_day_fires_again);
  RUN_TEST(test_unknown_time_does_nothing);
  RUN_TEST(test_disabled_and_last_wins);
  RUN_TEST(test_no_catching_up);
  RUN_TEST(test_json_round_trip);
  RUN_TEST(test_json_validation);
  RUN_TEST(test_config_keeps_the_schedule);
  return UNITY_END();
}

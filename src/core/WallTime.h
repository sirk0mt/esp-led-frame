// Local wall-clock time for effects and the schedule.
//
// A plain value: App fills it in the render task from the system clock (SNTP and the
// configured POSIX time zone), at most once per second, and effects read it through
// FrameContext. Tests set it by hand. Until the clock has been synchronized, known is
// false and the other fields are 0.
#pragma once

#include <cstdint>

namespace ledframe {

struct WallTime {
  bool known = false;   // the clock was synchronized; the fields below are valid
  uint16_t year = 0;    // e.g. 2026
  uint8_t month = 0;    // 1..12
  uint8_t day = 0;      // 1..31
  uint8_t hour = 0;     // 0..23
  uint8_t minute = 0;   // 0..59
  uint8_t second = 0;   // 0..59 (60 in a leap second)
  uint8_t weekday = 0;  // 0 = Monday .. 6 = Sunday (ISO order, not struct tm's)

  // Minutes since midnight, 0..1439.
  uint16_t minuteOfDay() const { return static_cast<uint16_t>(hour * 60 + minute); }
};

inline bool operator==(const WallTime& a, const WallTime& b) {
  return a.known == b.known && a.year == b.year && a.month == b.month && a.day == b.day &&
         a.hour == b.hour && a.minute == b.minute && a.second == b.second && a.weekday == b.weekday;
}
inline bool operator!=(const WallTime& a, const WallTime& b) {
  return !(a == b);
}

}  // namespace ledframe

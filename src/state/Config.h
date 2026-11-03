// Device configuration stored in /config.json.
//
// Serialization is a pure function so it is tested natively. Every field is
// validated on its own: an invalid or missing field keeps its default, and a file
// that does not parse gives the full default configuration.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"
#include "render/Layout.h"
#include "render/OutputMapping.h"
#include "state/Schedule.h"

namespace ledframe {

struct Config {
  static constexpr size_t kHostnameMax = 32;
  static constexpr size_t kSsidMax = 32;
  static constexpr size_t kPasswordMax = 64;
  static constexpr size_t kTimezoneMax = 63;
  static constexpr size_t kNtpServerMax = 64;
  static constexpr uint8_t kDefaultFps = 50;
  static constexpr uint8_t kDefaultVolts = 5;
  static constexpr uint32_t kDefaultMilliamps = 2000;
  static constexpr uint32_t kMaxMilliamps = 20000;

  LayoutConfig layout;
  uint8_t colorOrder = static_cast<uint8_t>(kDefaultColorOrder);
  uint8_t ledPin = kDefaultLedPin;  // strip data GPIO, one of LedPins; applies after a restart
  uint8_t powerVolts = kDefaultVolts;
  uint32_t powerMilliamps = kDefaultMilliamps;  // 0 = no limit
  uint8_t fps = kDefaultFps;
  bool dither = true;  // FastLED temporal dithering
  bool gamma = true;   // gamma correction of user colors (on by default since 2.1.0)
  char hostname[kHostnameMax + 1] = "ledframe";
  char wifiSsid[kSsidMax + 1] = "";
  char wifiPassword[kPasswordMax + 1] = "";  // secret: never logged or returned by the API
  char otaPassword[kPasswordMax + 1] = "";   // secret: never logged or returned by the API
  // POSIX TZ rule for local time; the default is Central European Time with
  // its summer time.
  char timezone[kTimezoneMax + 1] = "CET-1CEST,M3.5.0,M10.5.0/3";
  char ntpServer[kNtpServerMax + 1] = "pool.ntp.org";  // SNTP server, host name or IPv4
  Schedule schedule;                                   // on/off times; empty by default
};

bool operator==(const Config& a, const Config& b);
inline bool operator!=(const Config& a, const Config& b) {
  return !(a == b);
}

// Origin ids used in JSON: "top-left", "top-right", "bottom-left", "bottom-right".
const char* originId(Origin origin);
bool parseOrigin(const char* id, Origin& out);

// Hostname rules for mDNS: 1..32 characters, lowercase letters, digits and '-',
// not starting or ending with '-'.
bool isValidHostname(const char* hostname);

// POSIX TZ rule check: 1..63 characters, starting with a letter or '<' (a
// quoted name such as "<+03>-3"), then only letters, digits and + - , . / : < >.
// It catches typing errors; whether the rule means what the user wants is up to them.
bool isValidTimezone(const char* timezone);

// NTP server check: 1..64 characters, letters, digits, '.' and '-'.
bool isValidNtpServer(const char* server);

// A network name of spaces only (e.g. a field "cleared" with a space) means no
// network: it becomes "". Otherwise the device would keep trying to join it and
// take the radio away from its access point.
void clearBlankSsid(char* ssid);

// Writes JSON to out. Returns the number of bytes the writer took; a short count
// means a write error.
size_t serializeConfig(const Config& config, IByteWriter& out);
// Buffer form: returns the length without the terminating zero, or 0 when out is
// too small.
size_t serializeConfig(const Config& config, char* out, size_t capacity);

// Reads JSON into out. Returns false when the text does not parse or ends early;
// out then holds the defaults. Invalid fields keep their defaults.
bool deserializeConfig(IByteReader& in, Config& out);
bool deserializeConfig(const char* json, size_t length, Config& out);

}  // namespace ledframe

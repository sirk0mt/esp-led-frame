// JSON side of the REST API (docs/API.md), as pure functions tested natively.
//
// Parsers turn request bodies into engine commands or configuration changes and
// report validation errors as HTTP status codes. Writers serialize responses into a
// caller-provided buffer and return the length, or 0 when the buffer is too small.
// Like snprintf, a writer called with out = nullptr and capacity = 0 writes nothing
// and returns the length the response needs, without the terminating '\0'.
// Effects and parameters are described only from their descriptors, so adding an
// effect never requires changes here.
//
// Rules: a body must be a JSON object; a known field with a wrong type or an out of
// range value is a 400 error; unknown fields are ignored and reported in
// IgnoredFields for logging.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/CrashInfo.h"
#include "app/StateSnapshot.h"
#include "core/WallTime.h"
#include "effects/Command.h"
#include "effects/EffectRegistry.h"
#include "net/WifiTypes.h"
#include "render/Palettes.h"
#include "state/Config.h"
#include "state/Presets.h"

namespace ledframe {

struct ApiError {
  int status = 0;            // 0 = no error, otherwise an HTTP status code
  const char* message = "";  // static text for {"error": ...}

  bool ok() const { return status == 0; }
  bool set(int code, const char* text) {
    status = code;
    message = text;
    return false;
  }
};

struct IgnoredFields {
  static constexpr uint8_t kMaxNames = 4;
  static constexpr uint8_t kNameMax = 23;

  char names[kMaxNames][kNameMax + 1] = {};
  uint8_t count = 0;  // all ignored fields, also those not stored

  void add(const char* name);
};

struct CommandBatch {
  static constexpr uint8_t kMaxCommands = 6;

  Command commands[kMaxCommands];
  uint8_t count = 0;

  bool add(const Command& command);
};

// --- State ------------------------------------------------------------------

// POST /api/state and WebSocket text messages. Fields: "on" (bool), "bri"
// (0..255), "transition" (0..10000 ms), "effect" (id), "params" (object of
// parameter id -> value, for "effect" or the active effect), "palette" (index or
// id). Parameter values are integers; Toggle also takes true/false and Color also
// "#RRGGBB". The engine clamps parameter values to their range. Commands are
// ordered so that new parameters apply before the effect switch and the palette
// after it.
bool parseStatePatch(const char* body, size_t length, const EffectRegistry& registry,
                     uint8_t activeEffect, CommandBatch& out, ApiError& error,
                     IgnoredFields* ignored = nullptr);

// GET /api/state: {"version","on","bri","effect","transition","palette","params"}.
size_t writeState(const StateSnapshot& state, const EffectRegistry& registry, char* out,
                  size_t capacity);

// --- Effects and palettes ---------------------------------------------------

// GET /api/effects: visible effects in UI order with their parameter schema.
size_t writeEffects(const EffectRegistry& registry, char* out, size_t capacity);

// GET /api/palettes: every palette with sampleCount "#RRGGBB" preview colors.
size_t writePalettes(const Palettes& palettes, uint8_t sampleCount, char* out, size_t capacity);

// --- Configuration ----------------------------------------------------------

// GET /api/config: the configuration without passwords; "hasPassword" tells
// whether the Wi-Fi and OTA passwords are set.
size_t writeConfig(const Config& config, char* out, size_t capacity);

// POST /api/config: applies the fields in the body to config. On error config is
// unchanged. A missing password keeps the current one; "" clears it.
bool parseConfigPatch(const char* body, size_t length, Config& config, ApiError& error,
                      IgnoredFields* ignored = nullptr);

// --- Presets ----------------------------------------------------------------

// GET /api/presets: [{"id","name","effect","bri","palette","params"}].
size_t writePresets(const Presets& presets, const EffectRegistry& registry, char* out,
                    size_t capacity);

// POST /api/presets: {"name": "..."}.
bool parsePresetName(const char* body, size_t length, char (&name)[Preset::kNameMax + 1],
                     ApiError& error);

enum class PresetRoute : uint8_t { kInvalid, kItem, kApply };

// "/api/presets/7" is kItem, "/api/presets/7/apply" is kApply, with id 7 (1..255).
PresetRoute parsePresetPath(const char* path, uint8_t& id);

// --- System -----------------------------------------------------------------

struct SystemInfo {
  const char* version = "";
  const char* variant = "";  // build variant: the release file esp-led-frame-<version>-<variant>-ota.bin
  const char* repo = "";     // GitHub repository "owner/name" with the releases
  const char* buildDate = "";
  uint32_t uptimeS = 0;
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint16_t ledCount = 0;
  uint16_t maxLeds = 0;      // MAX_LEDS: limit of layout width * height
  bool testPattern = false;  // the test pattern is on the LEDs
  uint8_t ledPin = 0;        // strip data GPIO used since start (config.ledPin after a restart)
  uint8_t fps = 0;
  EngineStats stats;
  WifiStatus wifi;
  const CrashInfo* crash = nullptr;  // reset reason and last crash; nullptr = unknown
  WallTime time;                     // local time; known = false before SNTP sync
  bool sntp = false;                 // SNTP is running (started after the STA connected)
};

// GET /api/info. "time": {"synced": false, "sntp": bool} before SNTP synchronized
// the clock, then also "local": "YYYY-MM-DD HH:MM:SS" and "weekday": 0..6 (Monday 0).
// "sntp" tells whether synchronization runs: false in AP mode or before the station
// connected.
size_t writeInfo(const SystemInfo& info, char* out, size_t capacity);

// Captive portal: true when the Host header names the device itself (the AP address,
// with or without a port, or <hostname>.local in any letter case). Every other host
// is redirected to the setup page in AP mode.
bool isDeviceHost(const char* host, const char* apAddress, const char* hostname);

// Guard against cross-site requests (CSRF) and cross-site WebSocket hijacking: a web
// page on another site may not change the device through the visitor's browser.
// origin is the Origin header (nullptr when absent), host the Host header. Without
// an Origin header the request did not come from a cross-site browser page (curl,
// scripts), so it passes. With one, it passes only when the page came from the
// device itself: "http://" or "https://" followed by exactly the Host header, in any
// letter case. "null", other hosts, other ports and anything with a path fail.
bool isSameOrigin(const char* origin, const char* host);

// Guard against DNS rebinding: a site whose name first points to its own server and
// then to the device is, for the browser, the same origin as the device, so
// isSameOrigin() passes it. The browser still sends that site's name as Host, so the
// device answers only under a name nobody outside the local network can own. host is
// the Host header; a port and one trailing dot are ignored, letter
// case too. Device names:
//   - an IPv4 address, or an IPv6 address in brackets;
//   - "localhost" (an SSH tunnel or port forward on the user's own computer);
//   - hostname itself, or hostname followed by a domain that ends in one of the
//     local-only domains local, lan, home, home.arpa, internal, intranet, localdomain,
//     corp, private or box, e.g. "ledframe.local", "ledframe.lan", "ledframe.fritz.box".
// "ledframe.example.com" is not one: anyone can register a public domain and name a
// subdomain after the default hostname.
bool isDeviceName(const char* host, const char* hostname);

// The two guards above for one request. Returns nullptr when the request may pass,
// otherwise the reason for a 403: a Host header that is not a device name, or an
// Origin header of another site. Every request is checked, not only those that look
// like a browser's: over plain HTTP browsers send no Sec-Fetch-* headers, and a
// same-origin GET has no Origin either. An empty Host passes: browsers always send one.
const char* refuseRequest(const char* origin, const char* host, const char* hostname);

// POST /api/test-pattern: {"on": bool}.
bool parseTestPattern(const char* body, size_t length, bool& on, ApiError& error);

// GET /api/wifi/scan: {"scanning": bool, "networks": [{"ssid","rssi","secure"}]}.
size_t writeScan(const WifiNetwork* networks, uint8_t count, bool scanning, char* out,
                 size_t capacity);

// {"error": message}.
size_t writeError(const char* message, char* out, size_t capacity);

}  // namespace ledframe

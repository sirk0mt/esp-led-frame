#include <unity.h>

#include <cstring>

#include "effects/EffectEngine.h"
#include "state/Config.h"
#include "state/State.h"

using ledframe::Config;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectEngine;
using ledframe::EffectRegistry;
using ledframe::Frame;
using ledframe::FrameContext;
using ledframe::IFrameSink;
using ledframe::Origin;
using ledframe::Palettes;
using ledframe::ParamKind;
using ledframe::ParamSpec;
using ledframe::Rotation;
using ledframe::State;

namespace {

// --- Test effects -----------------------------------------------------------

class NullEffect : public Effect {
 public:
  bool render(FrameContext& /*ctx*/) override { return false; }
};
Effect* makeNull(void* memory) {
  return new (memory) NullEffect();
}

constexpr ParamSpec kSolidParams[] = {{"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFFFFF}};
constexpr ParamSpec kLevelParams[] = {
    {"level", "Level", ParamKind::Slider, 0, 100, 50},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};
// The same effect in a newer firmware: parameters reordered and one added.
constexpr ParamSpec kLevelParamsV2[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 10, 7},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
    {"level", "Level", ParamKind::Slider, 0, 100, 50},
};

const EffectDescriptor kSolid = {"solid", "Solid", 1, 0, kSolidParams, 1, nullptr, &makeNull};
const EffectDescriptor kLevel = {"level", "Level", 2, 0, kLevelParams, 2, nullptr, &makeNull};
const EffectDescriptor kPlain = {"plain", "Plain", 3, 0, nullptr, 0, nullptr, &makeNull};
const EffectDescriptor kLevelV2 = {"level", "Level", 0, 0, kLevelParamsV2, 3, nullptr, &makeNull};

const EffectRegistry& registry() {
  static EffectRegistry r;
  static bool filled = false;
  if (!filled) {
    r.add(&kSolid);
    r.add(&kLevel);
    r.add(&kPlain);
    filled = true;
  }
  return r;
}

// Newer firmware: "level" moved to the front with a new parameter, "plain" removed.
const EffectRegistry& registryV2() {
  static EffectRegistry r;
  static bool filled = false;
  if (!filled) {
    r.add(&kSolid);
    r.add(&kLevelV2);
    filled = true;
  }
  return r;
}

uint8_t index(const EffectRegistry& r, const char* id) {
  return static_cast<uint8_t>(r.indexOf(id));
}

class NullSink : public IFrameSink {
 public:
  void show(const Frame& /*frame*/) override {}
};

char buffer[4096];
State state;
State loaded;
Palettes palettes;

bool parseState(const char* json, const EffectRegistry& r = registry()) {
  return ledframe::deserializeState(json, std::strlen(json), r, loaded);
}

bool parseConfig(const char* json, Config& out) {
  return ledframe::deserializeConfig(json, std::strlen(json), out);
}

void expectDefaultConfig(const Config& config) {
  TEST_ASSERT_TRUE(config == Config{});
}

}  // namespace

void setUp() {
  state.reset(registry());
}
void tearDown() {}

// --- Config -----------------------------------------------------------------

void test_config_default_round_trip() {
  const size_t length = ledframe::serializeConfig(Config{}, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);
  Config out;
  out.fps = 99;
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(buffer, length, out));
  expectDefaultConfig(out);
}

void test_config_custom_round_trip() {
  Config config;
  config.layout.width = 8;
  config.layout.height = 32;
  config.layout.serpentine = true;
  config.layout.origin = Origin::kBottomRight;
  config.layout.rotation = Rotation::k90;
  config.layout.flipX = true;
  config.colorOrder = 5;
  config.ledPin = 18;
  config.powerVolts = 12;
  config.powerMilliamps = 0;
  config.fps = 60;
  config.dither = false;
  config.gamma = false;
  std::strcpy(config.hostname, "frame-2");
  std::strcpy(config.wifiSsid, "Home WiFi");
  std::strcpy(config.wifiPassword, "p@ss \"word\"");
  std::strcpy(config.otaPassword, "ota");
  const size_t length = ledframe::serializeConfig(config, buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"origin\":\"bottom-right\""));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"dither\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"gamma\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"ledPin\":18"));
  Config out;
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(buffer, length, out));
  TEST_ASSERT_TRUE(out == config);
}

void test_config_broken_file_gives_defaults() {
  const char* broken[] = {"", "{", "not json", "[1,2,3]", "{\"fps\":", "42"};
  for (const char* text : broken) {
    Config out;
    out.fps = 99;
    TEST_ASSERT_FALSE_MESSAGE(parseConfig(text, out), text);
    expectDefaultConfig(out);
  }
  Config out;
  TEST_ASSERT_FALSE(ledframe::deserializeConfig(nullptr, 0, out));
}

void test_config_invalid_fields_keep_defaults() {
  Config out;
  TEST_ASSERT_TRUE(
      parseConfig("{\"colorOrder\":9,\"ledPin\":12,\"fps\":5,\"dither\":\"no\",\"gamma\":5,"
                  "\"hostname\":\"Bad_Name\","
                  "\"power\":{\"volts\":0,\"milliamps\":999999},"
                  "\"wifi\":{\"ssid\":\"012345678901234567890123456789012\"},"
                  "\"layout\":{\"width\":4,\"height\":4,\"origin\":\"middle\"}}",
                  out));
  const Config defaults;
  TEST_ASSERT_EQUAL_UINT8(defaults.colorOrder, out.colorOrder);
  TEST_ASSERT_EQUAL_UINT8(ledframe::kDefaultLedPin, out.ledPin);  // 12: a boot strapping pin
  TEST_ASSERT_EQUAL_UINT8(defaults.fps, out.fps);
  TEST_ASSERT_EQUAL_STRING("ledframe", out.hostname);
  TEST_ASSERT_EQUAL_UINT8(defaults.powerVolts, out.powerVolts);
  TEST_ASSERT_EQUAL_UINT32(defaults.powerMilliamps, out.powerMilliamps);
  TEST_ASSERT_EQUAL_STRING("", out.wifiSsid);  // 33 characters: too long.
  TEST_ASSERT_TRUE(out.dither);                // "no" is not a bool: the default stays.
  TEST_ASSERT_TRUE(out.gamma);
  // An unknown origin keeps the default origin but the rest of the layout is used.
  TEST_ASSERT_EQUAL_UINT16(4, out.layout.width);
  TEST_ASSERT_TRUE(out.layout.origin == Origin::kTopLeft);

  // A stored name of spaces only (saved by older firmware) means no network.
  TEST_ASSERT_TRUE(parseConfig("{\"wifi\":{\"ssid\":\" \",\"password\":\"secret123\"}}", out));
  TEST_ASSERT_EQUAL_STRING("", out.wifiSsid);
  TEST_ASSERT_TRUE(out.dither);  // A file from 2.0.0 has no "dither": on.
  TEST_ASSERT_TRUE(out.gamma);   // nor "gamma": on since 2.1.0.
  TEST_ASSERT_EQUAL_UINT8(ledframe::kDefaultLedPin, out.ledPin);  // nor "ledPin": LED_PIN

  TEST_ASSERT_TRUE(parseConfig("{\"ledPin\":300}", out));
  TEST_ASSERT_EQUAL_UINT8(ledframe::kDefaultLedPin, out.ledPin);
  TEST_ASSERT_TRUE(parseConfig("{\"ledPin\":33}", out));
  TEST_ASSERT_EQUAL_UINT8(33, out.ledPin);
}

void test_config_invalid_layout_falls_back_to_default_layout() {
  // Valid fields that together form an invalid layout: the whole default layout.
  const char* invalid[] = {
      "{\"layout\":{\"width\":4,\"height\":4,\"rotation\":45}}",
      "{\"layout\":{\"width\":64,\"height\":64}}",  // 4096 LEDs > kMaxLeds
  };
  for (const char* text : invalid) {
    Config out;
    TEST_ASSERT_TRUE(parseConfig(text, out));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(15, out.layout.width, text);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(10, out.layout.height, text);
  }
  // An invalid single field (zero, wrong type) keeps its default; the rest is used.
  const char* badWidth[] = {
      "{\"layout\":{\"width\":0,\"height\":4}}",
      "{\"layout\":{\"width\":\"wide\",\"height\":4}}",
  };
  for (const char* text : badWidth) {
    Config out;
    TEST_ASSERT_TRUE(parseConfig(text, out));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(15, out.layout.width, text);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(4, out.layout.height, text);
  }
}

// Time zone and NTP server: kept through a round trip, defaults without them.
void test_config_time_fields() {
  Config config;
  std::strcpy(config.timezone, "<+03>-3");
  std::strcpy(config.ntpServer, "time.example.org");
  const size_t length = ledframe::serializeConfig(config, buffer, sizeof(buffer));
  Config out;
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(buffer, length, out));
  TEST_ASSERT_EQUAL_STRING("<+03>-3", out.timezone);
  TEST_ASSERT_EQUAL_STRING("time.example.org", out.ntpServer);
  TEST_ASSERT_TRUE(out == config);

  const char kOld[] = "{\"fps\":60}";  // a file from before 2.2.0
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(kOld, std::strlen(kOld), out));
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", out.timezone);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", out.ntpServer);

  const char kBad[] = "{\"timezone\":\"CET-1 CEST\",\"ntpServer\":\"pool ntp\"}";
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(kBad, std::strlen(kBad), out));
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", out.timezone);  // invalid: default
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", out.ntpServer);
}

// The time zone check catches length and character errors.
void test_timezone_rules() {
  TEST_ASSERT_TRUE(ledframe::isValidTimezone("CET-1CEST,M3.5.0,M10.5.0/3"));
  TEST_ASSERT_TRUE(ledframe::isValidTimezone("UTC0"));
  TEST_ASSERT_TRUE(ledframe::isValidTimezone("EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00"));
  TEST_ASSERT_TRUE(ledframe::isValidTimezone("<+0545>-5:45"));
  TEST_ASSERT_FALSE(ledframe::isValidTimezone(""));
  TEST_ASSERT_FALSE(ledframe::isValidTimezone(nullptr));
  TEST_ASSERT_FALSE(ledframe::isValidTimezone("1CET"));      // must start with a name
  TEST_ASSERT_FALSE(ledframe::isValidTimezone("CET -1"));    // space
  TEST_ASSERT_FALSE(ledframe::isValidTimezone("CET-1;rm"));  // ';'
  TEST_ASSERT_FALSE(ledframe::isValidTimezone("CET\"1"));    // quote
  char tooLong[Config::kTimezoneMax + 2];
  std::memset(tooLong, 'A', sizeof(tooLong) - 1);
  tooLong[sizeof(tooLong) - 1] = '\0';
  TEST_ASSERT_FALSE(ledframe::isValidTimezone(tooLong));  // 64 characters
  tooLong[Config::kTimezoneMax] = '\0';
  TEST_ASSERT_TRUE(ledframe::isValidTimezone(tooLong));  // 63 is the limit

  TEST_ASSERT_TRUE(ledframe::isValidNtpServer("pool.ntp.org"));
  TEST_ASSERT_TRUE(ledframe::isValidNtpServer("192.168.0.1"));
  TEST_ASSERT_FALSE(ledframe::isValidNtpServer(""));
  TEST_ASSERT_FALSE(ledframe::isValidNtpServer("pool.ntp.org:123"));
  TEST_ASSERT_FALSE(ledframe::isValidNtpServer("http://pool"));
}

void test_config_small_buffer() {
  char small[16];
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializeConfig(Config{}, small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializeConfig(Config{}, nullptr, 100));
}

void test_origin_and_hostname_helpers() {
  Origin origin = Origin::kTopLeft;
  TEST_ASSERT_TRUE(ledframe::parseOrigin("bottom-left", origin));
  TEST_ASSERT_TRUE(origin == Origin::kBottomLeft);
  TEST_ASSERT_EQUAL_STRING("bottom-left", ledframe::originId(origin));
  TEST_ASSERT_FALSE(ledframe::parseOrigin("left", origin));
  TEST_ASSERT_FALSE(ledframe::parseOrigin(nullptr, origin));
  TEST_ASSERT_TRUE(ledframe::isValidHostname("led-frame1"));
  TEST_ASSERT_FALSE(ledframe::isValidHostname(""));
  TEST_ASSERT_FALSE(ledframe::isValidHostname("-frame"));
  TEST_ASSERT_FALSE(ledframe::isValidHostname("frame-"));
  TEST_ASSERT_FALSE(ledframe::isValidHostname("Frame"));
  TEST_ASSERT_FALSE(ledframe::isValidHostname("frame.local"));
}

// --- State ------------------------------------------------------------------

void test_state_defaults() {
  TEST_ASSERT_TRUE(state.on);
  TEST_ASSERT_EQUAL_UINT8(128, state.brightness);
  TEST_ASSERT_EQUAL_UINT8(index(registry(), "solid"), state.effect);
  TEST_ASSERT_EQUAL_UINT32(500, state.transitionMs);
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, state.params[index(registry(), "solid")][0]);
  TEST_ASSERT_EQUAL_INT32(50, state.params[index(registry(), "level")][0]);
}

void test_state_round_trip() {
  const uint8_t level = index(registry(), "level");
  state.on = false;
  state.brightness = 42;
  state.effect = level;
  state.transitionMs = 1200;
  state.palette = 3;
  state.userColors[1] = 0x123456;
  state.params[index(registry(), "solid")][0] = 0xFF0000;
  state.params[level][0] = 7;
  state.params[level][1] = 5;
  const size_t length = ledframe::serializeState(state, registry(), buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"effect\":\"level\""));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"level\":{\"level\":7,\"palette\":5}"));
  TEST_ASSERT_NULL(std::strstr(buffer, "\"plain\""));  // No parameters, no entry.

  TEST_ASSERT_TRUE(ledframe::deserializeState(buffer, length, registry(), loaded));
  TEST_ASSERT_FALSE(loaded.on);
  TEST_ASSERT_EQUAL_UINT8(42, loaded.brightness);
  TEST_ASSERT_EQUAL_UINT8(level, loaded.effect);
  TEST_ASSERT_EQUAL_UINT32(1200, loaded.transitionMs);
  TEST_ASSERT_EQUAL_INT32(3, loaded.palette);
  TEST_ASSERT_EQUAL_HEX32(0x123456, loaded.userColors[1]);
  TEST_ASSERT_EQUAL_INT32(0xFF0000, loaded.params[index(registry(), "solid")][0]);
  TEST_ASSERT_EQUAL_INT32(7, loaded.params[level][0]);
  TEST_ASSERT_EQUAL_INT32(5, loaded.params[level][1]);
}

void test_state_broken_file_gives_defaults() {
  const char* broken[] = {"", "{\"on\":", "garbage", "[]", "null"};
  for (const char* text : broken) {
    loaded.brightness = 1;
    TEST_ASSERT_FALSE_MESSAGE(parseState(text), text);
    TEST_ASSERT_EQUAL_UINT8(128, loaded.brightness);
    TEST_ASSERT_EQUAL_UINT8(index(registry(), "solid"), loaded.effect);
  }
}

void test_state_unknown_ids_are_skipped() {
  TEST_ASSERT_TRUE(
      parseState("{\"effect\":\"galaxy\",\"params\":{\"galaxy\":{\"density\":5},"
                 "\"level\":{\"level\":9,\"glow\":3},\"solid\":7}}"));
  TEST_ASSERT_EQUAL_UINT8(index(registry(), "solid"), loaded.effect);  // Unknown: default.
  TEST_ASSERT_EQUAL_INT32(9, loaded.params[index(registry(), "level")][0]);
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, loaded.params[index(registry(), "solid")][0]);
}

void test_state_values_are_clamped_or_ignored() {
  TEST_ASSERT_TRUE(parseState(
      "{\"bri\":300,\"on\":\"yes\",\"transition\":99999,\"palette\":42,"
      "\"userColors\":[1,2],"
      "\"params\":{\"level\":{\"level\":500,\"palette\":99},\"solid\":{\"color\":\"red\"}}}"));
  TEST_ASSERT_EQUAL_UINT8(128, loaded.brightness);  // Out of range: default.
  TEST_ASSERT_TRUE(loaded.on);                      // Wrong type: default.
  TEST_ASSERT_EQUAL_UINT32(EffectEngine::kMaxTransitionMs, loaded.transitionMs);
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, loaded.palette);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, loaded.userColors[0]);  // Wrong length: defaults.
  TEST_ASSERT_EQUAL_INT32(100, loaded.params[index(registry(), "level")][0]);
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, loaded.params[index(registry(), "level")][1]);
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, loaded.params[index(registry(), "solid")][0]);
}

void test_state_survives_new_firmware_schema() {
  const uint8_t level = index(registry(), "level");
  state.effect = level;
  state.params[level][0] = 33;
  state.params[level][1] = 4;
  const size_t length = ledframe::serializeState(state, registry(), buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);

  // In the new schema "level" is index 0 and its parameters moved.
  TEST_ASSERT_TRUE(ledframe::deserializeState(buffer, length, registryV2(), loaded));
  const uint8_t levelV2 = index(registryV2(), "level");
  TEST_ASSERT_EQUAL_UINT8(levelV2, loaded.effect);
  TEST_ASSERT_EQUAL_INT32(7, loaded.params[levelV2][0]);   // New "speed": default.
  TEST_ASSERT_EQUAL_INT32(4, loaded.params[levelV2][1]);   // "palette"
  TEST_ASSERT_EQUAL_INT32(33, loaded.params[levelV2][2]);  // "level"
}

void test_state_small_buffer() {
  char small[8];
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializeState(state, registry(), small, sizeof(small)));
}

void test_apply_and_capture_round_trip() {
  static NullSink sink;
  static EffectEngine engine(registry(), palettes, sink);
  const uint8_t level = index(registry(), "level");
  state.on = false;
  state.brightness = 77;
  state.effect = level;
  state.transitionMs = 250;
  state.palette = 6;
  state.userColors[2] = 0x00FFFF;
  state.params[level][0] = 12;
  state.params[index(registry(), "solid")][0] = 0x00FF00;

  ledframe::applyState(state, registry(), palettes, engine);
  TEST_ASSERT_FALSE(engine.on());
  TEST_ASSERT_EQUAL_UINT8(77, engine.brightness());
  TEST_ASSERT_EQUAL_UINT8(level, engine.activeEffect());
  TEST_ASSERT_EQUAL_UINT32(250, engine.transitionMs());
  TEST_ASSERT_EQUAL_INT32(6, engine.paletteIndex());
  TEST_ASSERT_EQUAL_INT32(12, engine.params(level)[0]);
  TEST_ASSERT_EQUAL_INT32(6, engine.params(level)[1]);  // SetPalette reached the param.
  TEST_ASSERT_EQUAL_UINT8(0, palettes.userColor(2).r);
  TEST_ASSERT_EQUAL_UINT8(255, palettes.userColor(2).b);

  State captured;
  captured.reset(registry());
  ledframe::captureState(engine, registry(), palettes, captured);
  TEST_ASSERT_FALSE(captured.on);
  TEST_ASSERT_EQUAL_UINT8(77, captured.brightness);
  TEST_ASSERT_EQUAL_UINT8(level, captured.effect);
  TEST_ASSERT_EQUAL_UINT32(250, captured.transitionMs);
  TEST_ASSERT_EQUAL_INT32(6, captured.palette);
  TEST_ASSERT_EQUAL_HEX32(0x00FFFF, captured.userColors[2]);
  TEST_ASSERT_EQUAL_INT32(12, captured.params[level][0]);
  TEST_ASSERT_EQUAL_INT32(0x00FF00, captured.params[index(registry(), "solid")][0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_config_default_round_trip);
  RUN_TEST(test_config_custom_round_trip);
  RUN_TEST(test_config_broken_file_gives_defaults);
  RUN_TEST(test_config_invalid_fields_keep_defaults);
  RUN_TEST(test_config_invalid_layout_falls_back_to_default_layout);
  RUN_TEST(test_config_time_fields);
  RUN_TEST(test_timezone_rules);
  RUN_TEST(test_config_small_buffer);
  RUN_TEST(test_origin_and_hostname_helpers);
  RUN_TEST(test_state_defaults);
  RUN_TEST(test_state_round_trip);
  RUN_TEST(test_state_broken_file_gives_defaults);
  RUN_TEST(test_state_unknown_ids_are_skipped);
  RUN_TEST(test_state_values_are_clamped_or_ignored);
  RUN_TEST(test_state_survives_new_firmware_schema);
  RUN_TEST(test_state_small_buffer);
  RUN_TEST(test_apply_and_capture_round_trip);
  return UNITY_END();
}

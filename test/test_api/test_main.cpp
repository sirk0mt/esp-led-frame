#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>

#include "net/ApiJson.h"

using ledframe::ApiError;
using ledframe::CommandBatch;
using ledframe::CommandType;
using ledframe::Config;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::IgnoredFields;
using ledframe::Palettes;
using ledframe::ParamKind;
using ledframe::ParamSpec;
using ledframe::Preset;
using ledframe::PresetRoute;
using ledframe::Presets;
using ledframe::StateSnapshot;

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

namespace {

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
    {"mirror", "Mirror", ParamKind::Toggle, 0, 1, 0},
    {"mode", "Mode", ParamKind::Select, 0, 0, 1, "random|palette|solid"},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

const EffectDescriptor kSolid = {"solid",      "Solid", 10,      ledframe::kFlagStatic,
                                 kSolidParams, 1,       nullptr, &makeNull};
const EffectDescriptor kLevel = {
    "level",      "Level", 20,      ledframe::kFlag2D | ledframe::kFlagPalette,
    kLevelParams, 4,       "paint", &makeNull};
const EffectDescriptor kStatus = {"status", "Status", 5,       ledframe::kFlagHidden,
                                  nullptr,  0,        nullptr, &makeNull};

const EffectRegistry& registry() {
  static EffectRegistry r;
  static bool filled = false;
  if (!filled) {
    r.add(&kSolid);
    r.add(&kLevel);
    r.add(&kStatus);
    filled = true;
  }
  return r;
}

uint8_t indexOf(const char* id) {
  return static_cast<uint8_t>(registry().indexOf(id));
}

CommandBatch batch;
ApiError error;
IgnoredFields ignored;
char buffer[8192];
JsonDocument doc;

bool patch(const char* json, uint8_t active) {
  ignored = IgnoredFields{};
  return ledframe::parseStatePatch(json, std::strlen(json), registry(), active, batch, error,
                                   &ignored);
}

bool patchSolid(const char* json) {
  return patch(json, indexOf("solid"));
}

void expectBadState(const char* json, const char* message = nullptr) {
  TEST_ASSERT_FALSE_MESSAGE(patchSolid(json), json);
  TEST_ASSERT_EQUAL_INT_MESSAGE(400, error.status, json);
  if (message != nullptr) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(message, error.message, json);
  }
}

void parseOutput(size_t length) {
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_TRUE(deserializeJson(doc, buffer, length) == DeserializationError::Ok);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- POST /api/state ----------------------------------------------------------

void test_state_single_field() {
  TEST_ASSERT_TRUE(patchSolid("{\"bri\":200}"));
  TEST_ASSERT_TRUE(error.ok());
  TEST_ASSERT_EQUAL_UINT8(1, batch.count);
  ASSERT_ENUM(CommandType::SetBrightness, batch.commands[0].type);
  TEST_ASSERT_EQUAL_UINT8(200, batch.commands[0].brightness);
}

void test_state_empty_object() {
  TEST_ASSERT_TRUE(patchSolid("{}"));
  TEST_ASSERT_EQUAL_UINT8(0, batch.count);
}

void test_state_command_order() {
  TEST_ASSERT_TRUE(
      patchSolid("{\"palette\":2,\"effect\":\"level\",\"params\":{\"level\":40},\"transition\":0,"
                 "\"bri\":10,\"on\":false}"));
  TEST_ASSERT_EQUAL_UINT8(6, batch.count);
  ASSERT_ENUM(CommandType::SetPower, batch.commands[0].type);
  TEST_ASSERT_FALSE(batch.commands[0].on);
  ASSERT_ENUM(CommandType::SetBrightness, batch.commands[1].type);
  ASSERT_ENUM(CommandType::SetTransition, batch.commands[2].type);
  TEST_ASSERT_EQUAL_UINT32(0, batch.commands[2].transitionMs);
  // Parameters of the new effect come before the switch, the palette after it.
  ASSERT_ENUM(CommandType::SetParams, batch.commands[3].type);
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), batch.commands[3].effect);
  TEST_ASSERT_EQUAL_UINT8(1, batch.commands[3].paramCount);
  TEST_ASSERT_EQUAL_INT32(40, batch.commands[3].params[0].value);
  ASSERT_ENUM(CommandType::SetEffect, batch.commands[4].type);
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), batch.commands[4].effect);
  ASSERT_ENUM(CommandType::SetPalette, batch.commands[5].type);
  TEST_ASSERT_EQUAL_INT32(2, batch.commands[5].palette);
}

void test_state_params_target_active_effect() {
  TEST_ASSERT_TRUE(
      patch("{\"params\":{\"mirror\":true,\"mode\":2,\"level\":-5}}", indexOf("level")));
  TEST_ASSERT_EQUAL_UINT8(1, batch.count);
  const auto& command = batch.commands[0];
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), command.effect);
  TEST_ASSERT_EQUAL_UINT8(3, command.paramCount);
  TEST_ASSERT_EQUAL_UINT8(1, command.params[0].index);  // mirror
  TEST_ASSERT_EQUAL_INT32(1, command.params[0].value);
  TEST_ASSERT_EQUAL_INT32(2, command.params[1].value);   // mode
  TEST_ASSERT_EQUAL_INT32(-5, command.params[2].value);  // out of range: the engine clamps
}

void test_state_color_values() {
  TEST_ASSERT_TRUE(patchSolid("{\"params\":{\"color\":\"#ff8000\"}}"));
  TEST_ASSERT_EQUAL_INT32(0xFF8000, batch.commands[0].params[0].value);
  TEST_ASSERT_TRUE(patchSolid("{\"params\":{\"color\":255}}"));
  TEST_ASSERT_EQUAL_INT32(255, batch.commands[0].params[0].value);
  expectBadState("{\"params\":{\"color\":\"red\"}}");
  expectBadState("{\"params\":{\"color\":\"#12345\"}}");
}

void test_state_palette_by_id() {
  TEST_ASSERT_TRUE(patchSolid("{\"palette\":\"heat\"}"));
  TEST_ASSERT_EQUAL_INT32(Palettes::indexOf("heat"), batch.commands[0].palette);
  expectBadState("{\"palette\":\"nope\"}", "unknown palette");
  expectBadState("{\"palette\":8}", "unknown palette");
  expectBadState("{\"palette\":-1}", "unknown palette");
  expectBadState("{\"palette\":true}");
}

void test_state_unknown_fields_are_ignored() {
  TEST_ASSERT_TRUE(patchSolid("{\"bri\":1,\"foo\":2,\"params\":{\"glow\":3,\"color\":4}}"));
  TEST_ASSERT_EQUAL_UINT8(2, batch.count);
  TEST_ASSERT_EQUAL_UINT8(2, ignored.count);
  TEST_ASSERT_EQUAL_STRING("foo", ignored.names[0]);
  TEST_ASSERT_EQUAL_STRING("glow", ignored.names[1]);
}

void test_state_invalid_input() {
  expectBadState("", "invalid JSON");
  expectBadState("{\"bri\":", "invalid JSON");
  expectBadState("[1,2]", "body must be a JSON object");
  expectBadState("\"bri\"", "body must be a JSON object");
  expectBadState("{\"bri\":256}");
  expectBadState("{\"bri\":-1}");
  expectBadState("{\"bri\":\"high\"}");
  expectBadState("{\"bri\":1.5}");
  expectBadState("{\"on\":1}");
  expectBadState("{\"transition\":10001}");
  expectBadState("{\"effect\":3}");
  expectBadState("{\"effect\":\"galaxy\"}", "unknown effect");
  expectBadState("{\"effect\":\"status\"}", "unknown effect");  // Hidden.
  expectBadState("{\"params\":[1]}");
  expectBadState("{\"params\":{\"color\":true}}");
  TEST_ASSERT_EQUAL_UINT8(0, batch.count);
  // Nothing to target.
  TEST_ASSERT_FALSE(patch("{\"params\":{\"level\":1}}", 0xFF));
  TEST_ASSERT_EQUAL_STRING("no active effect for params", error.message);
  TEST_ASSERT_FALSE(ledframe::parseStatePatch(nullptr, 0, registry(), 0, batch, error));
}

// --- GET /api/state, /api/effects, /api/palettes -------------------------------

void test_write_state() {
  StateSnapshot state;
  state.version = 7;
  state.on = false;
  state.brightness = 99;
  state.effect = indexOf("level");
  state.palette = 3;
  state.transitionMs = 250;
  state.paramCount = 4;
  state.params[0] = 42;
  state.params[2] = 1;
  parseOutput(ledframe::writeState(state, registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT32(7, doc["version"].as<uint32_t>());
  TEST_ASSERT_FALSE(doc["on"].as<bool>());
  TEST_ASSERT_EQUAL_INT(99, doc["bri"].as<int>());
  TEST_ASSERT_EQUAL_STRING("level", doc["effect"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(250, doc["transition"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, doc["palette"].as<int>());
  TEST_ASSERT_EQUAL_INT(42, doc["params"]["level"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, doc["params"]["mode"].as<int>());

  state.effect = 0xFF;
  parseOutput(ledframe::writeState(state, registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_TRUE(doc["effect"].isNull());
  TEST_ASSERT_EQUAL_UINT32(0, doc["params"].size());
}

void test_write_effects_hides_hidden_and_describes_params() {
  parseOutput(ledframe::writeEffects(registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT32(2, doc.size());  // "status" is hidden.
  TEST_ASSERT_EQUAL_STRING("solid", doc[0]["id"].as<const char*>());
  TEST_ASSERT_TRUE(doc[0]["static"].as<bool>());
  TEST_ASSERT_TRUE(doc[0]["widget"].isNull());
  TEST_ASSERT_EQUAL_STRING("color", doc[0]["params"][0]["kind"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0xFFFFFF, doc[0]["params"][0]["default"].as<int>());

  JsonVariant level = doc[1];
  TEST_ASSERT_EQUAL_STRING("Level", level["name"].as<const char*>());
  TEST_ASSERT_TRUE(level["is2d"].as<bool>());
  TEST_ASSERT_TRUE(level["usesPalette"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("paint", level["widget"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4, level["params"].size());
  TEST_ASSERT_EQUAL_INT(100, level["params"][0]["max"].as<int>());
  TEST_ASSERT_EQUAL_STRING("toggle", level["params"][1]["kind"].as<const char*>());
  JsonVariant mode = level["params"][2];
  TEST_ASSERT_EQUAL_UINT32(3, mode["options"].size());
  TEST_ASSERT_EQUAL_STRING("random", mode["options"][0].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("solid", mode["options"][2].as<const char*>());
  TEST_ASSERT_EQUAL_INT(Palettes::kCount - 1, level["params"][3]["max"].as<int>());
}

void test_write_palettes() {
  static Palettes palettes;
  parseOutput(ledframe::writePalettes(palettes, 16, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT32(Palettes::kCount, doc.size());
  TEST_ASSERT_EQUAL_STRING("rainbow", doc[0]["id"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(16, doc[0]["colors"].size());
  TEST_ASSERT_EQUAL_UINT32(7, std::strlen(doc[0]["colors"][0].as<const char*>()));
  TEST_ASSERT_TRUE(doc[Palettes::kUserIndex]["custom"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("#FF0000", doc[Palettes::kUserIndex]["colors"][0].as<const char*>());
}

// --- Config ---------------------------------------------------------------------

void test_write_config_hides_secrets() {
  Config config;
  std::strcpy(config.wifiSsid, "Home");
  std::strcpy(config.wifiPassword, "supersecret");
  parseOutput(ledframe::writeConfig(config, buffer, sizeof(buffer)));
  TEST_ASSERT_NULL(std::strstr(buffer, "supersecret"));
  TEST_ASSERT_TRUE(doc["wifi"]["password"].isNull());
  TEST_ASSERT_TRUE(doc["wifi"]["hasPassword"].as<bool>());
  TEST_ASSERT_FALSE(doc["ota"]["hasPassword"].as<bool>());
  TEST_ASSERT_TRUE(doc["dither"].as<bool>());
  TEST_ASSERT_TRUE(doc["gamma"].as<bool>());
  TEST_ASSERT_TRUE(doc["gamma"].is<bool>());
  TEST_ASSERT_EQUAL_STRING("Home", doc["wifi"]["ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("top-left", doc["layout"]["origin"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", doc["timezone"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", doc["ntpServer"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(ledframe::kDefaultLedPin, doc["ledPin"].as<int>());
  TEST_ASSERT_TRUE(doc["schedule"].is<JsonArray>());  // empty list by default
  TEST_ASSERT_EQUAL_UINT32(0, doc["schedule"].size());
}

bool configPatch(const char* json, Config& config) {
  ignored = IgnoredFields{};
  return ledframe::parseConfigPatch(json, std::strlen(json), config, error, &ignored);
}

void test_config_patch_merges_fields() {
  Config config;
  std::strcpy(config.wifiPassword, "keepthisone");
  TEST_ASSERT_TRUE(
      configPatch("{\"layout\":{\"width\":20,\"serpentine\":true,\"origin\":\"bottom-right\"},"
                  "\"colorOrder\":2,\"ledPin\":18,\"fps\":60,\"dither\":false,\"gamma\":false,"
                  "\"power\":{\"milliamps\":0},"
                  "\"hostname\":\"frame-1\",\"wifi\":{\"ssid\":\"Cafe\"},\"extra\":1}",
                  config));
  TEST_ASSERT_EQUAL_UINT16(20, config.layout.width);
  TEST_ASSERT_EQUAL_UINT16(10, config.layout.height);  // Kept.
  TEST_ASSERT_TRUE(config.layout.serpentine);
  TEST_ASSERT_TRUE(config.layout.origin == ledframe::Origin::kBottomRight);
  TEST_ASSERT_EQUAL_UINT8(2, config.colorOrder);
  TEST_ASSERT_EQUAL_UINT8(18, config.ledPin);
  TEST_ASSERT_EQUAL_UINT8(60, config.fps);
  TEST_ASSERT_FALSE(config.dither);
  TEST_ASSERT_FALSE(config.gamma);
  TEST_ASSERT_EQUAL_UINT32(0, config.powerMilliamps);
  TEST_ASSERT_EQUAL_UINT8(5, config.powerVolts);  // Kept.
  TEST_ASSERT_EQUAL_STRING("frame-1", config.hostname);
  TEST_ASSERT_EQUAL_STRING("Cafe", config.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("keepthisone", config.wifiPassword);  // Not sent: kept.
  TEST_ASSERT_EQUAL_UINT8(1, ignored.count);

  TEST_ASSERT_TRUE(
      configPatch("{\"wifi\":{\"password\":\"\"},\"ota\":{\"password\":\"x\"}}", config));
  TEST_ASSERT_EQUAL_STRING("", config.wifiPassword);  // "" clears.
  TEST_ASSERT_EQUAL_STRING("x", config.otaPassword);

  // A name of spaces only means no network (access point only), like "".
  TEST_ASSERT_TRUE(configPatch("{\"wifi\":{\"ssid\":\"   \"}}", config));
  TEST_ASSERT_EQUAL_STRING("", config.wifiSsid);
  TEST_ASSERT_TRUE(
      configPatch("{\"wifi\":{\"ssid\":\"There's no place like 127.0.0.1\"}}", config));
  TEST_ASSERT_EQUAL_STRING("There's no place like 127.0.0.1", config.wifiSsid);
}

// Time zone and NTP server: set through the API, a bad value is a 400 error
// and leaves the configuration unchanged.
void test_config_patch_time_fields() {
  Config config;
  TEST_ASSERT_TRUE(configPatch(
      "{\"timezone\":\"GMT0BST,M3.5.0/1,M10.5.0\",\"ntpServer\":\"time.google.com\"}", config));
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", config.timezone);
  TEST_ASSERT_EQUAL_STRING("time.google.com", config.ntpServer);
  TEST_ASSERT_EQUAL_UINT8(0, ignored.count);

  TEST_ASSERT_FALSE(configPatch("{\"timezone\":\"GMT 0\"}", config));
  TEST_ASSERT_EQUAL_INT(400, error.status);
  TEST_ASSERT_NOT_NULL(std::strstr(error.message, "timezone"));
  TEST_ASSERT_FALSE(configPatch("{\"timezone\":5}", config));
  TEST_ASSERT_FALSE(configPatch("{\"ntpServer\":\"\"}", config));
  TEST_ASSERT_NOT_NULL(std::strstr(error.message, "ntpServer"));
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", config.timezone);  // unchanged
  TEST_ASSERT_EQUAL_STRING("time.google.com", config.ntpServer);
}

// Schedule: a patch replaces the whole list; a bad entry is a 400 error and
// leaves the old list.
void test_config_patch_schedule() {
  Config config;
  TEST_ASSERT_TRUE(
      configPatch("{\"schedule\":[{\"days\":[0,1,2,3,4],\"time\":\"07:30\",\"action\":\"on\"},"
                  "{\"enabled\":false,\"days\":[5,6],\"time\":\"23:15\",\"action\":\"off\"}]}",
                  config));
  TEST_ASSERT_EQUAL_UINT8(2, config.schedule.count);
  TEST_ASSERT_EQUAL_UINT8(0x1F, config.schedule.entries[0].days);
  TEST_ASSERT_TRUE(config.schedule.entries[0].turnOn);
  TEST_ASSERT_FALSE(config.schedule.entries[1].enabled);
  TEST_ASSERT_EQUAL_UINT8(23, config.schedule.entries[1].hour);
  TEST_ASSERT_EQUAL_UINT8(0, ignored.count);

  parseOutput(ledframe::writeConfig(config, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("23:15", doc["schedule"][1]["time"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("off", doc["schedule"][1]["action"].as<const char*>());

  TEST_ASSERT_FALSE(
      configPatch("{\"schedule\":[{\"days\":[0],\"time\":\"25:00\","
                  "\"action\":\"on\"}]}",
                  config));
  TEST_ASSERT_EQUAL_INT(400, error.status);
  TEST_ASSERT_NOT_NULL(std::strstr(error.message, "time"));
  TEST_ASSERT_EQUAL_UINT8(2, config.schedule.count);  // unchanged
  TEST_ASSERT_TRUE(configPatch("{\"schedule\":[]}", config));
  TEST_ASSERT_EQUAL_UINT8(0, config.schedule.count);
}

void test_config_patch_rejects_invalid_values() {
  const char* invalid[] = {
      "{\"layout\":{\"width\":0}}",
      "{\"layout\":{\"width\":64,\"height\":64}}",
      "{\"layout\":{\"rotation\":45}}",
      "{\"layout\":{\"origin\":\"middle\"}}",
      "{\"layout\":{\"serpentine\":\"yes\"}}",
      "{\"layout\":5}",
      "{\"colorOrder\":6}",
      "{\"ledPin\":12}",
      "{\"ledPin\":34}",
      "{\"ledPin\":-1}",
      "{\"ledPin\":\"4\"}",
      "{\"fps\":5}",
      "{\"fps\":121}",
      "{\"dither\":1}",
      "{\"gamma\":\"yes\"}",
      "{\"power\":{\"volts\":0}}",
      "{\"power\":{\"milliamps\":20001}}",
      "{\"hostname\":\"Bad Name\"}",
      "{\"hostname\":\"\"}",
      "{\"wifi\":{\"password\":\"short\"}}",
      "{\"wifi\":{\"ssid\":\"012345678901234567890123456789012\"}}",
      "{\"wifi\":\"Home\"}",
      "{\"ota\":{\"password\":5}}",
      "not json",
  };
  for (const char* text : invalid) {
    Config config;
    TEST_ASSERT_FALSE_MESSAGE(configPatch(text, config), text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, error.status, text);
    TEST_ASSERT_TRUE_MESSAGE(config == Config{}, text);  // Unchanged on error.
  }
}

// --- Presets ----------------------------------------------------------------------

void test_presets_json() {
  Presets presets;
  const int32_t values[] = {30, 1, 2, 4};
  presets.save(ledframe::makePreset("Evening glow", indexOf("level"), values, 4, 4, 60));
  parseOutput(ledframe::writePresets(presets, registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT32(1, doc.size());
  TEST_ASSERT_EQUAL_INT(1, doc[0]["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Evening glow", doc[0]["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("level", doc[0]["effect"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(30, doc[0]["params"]["level"].as<int>());

  char name[Preset::kNameMax + 1];
  const char* body = "{\"name\":\"Movie night\"}";
  TEST_ASSERT_TRUE(ledframe::parsePresetName(body, std::strlen(body), name, error));
  TEST_ASSERT_EQUAL_STRING("Movie night", name);
  const char* bad[] = {"{}", "{\"name\":\"\"}", "{\"name\":7}", "[]",
                       "{\"name\":\"012345678901234567890123456789012\"}"};
  for (const char* text : bad) {
    TEST_ASSERT_FALSE_MESSAGE(ledframe::parsePresetName(text, std::strlen(text), name, error),
                              text);
    TEST_ASSERT_EQUAL_INT(400, error.status);
  }
}

void test_captive_portal_hosts() {
  const char* ap = "192.168.4.1";
  const char* device[] = {"192.168.4.1", "192.168.4.1:80", "ledframe.local", "LedFrame.LOCAL",
                          "ledframe.local:80"};
  for (const char* host : device) {
    TEST_ASSERT_TRUE_MESSAGE(ledframe::isDeviceHost(host, ap, "ledframe"), host);
  }
  // Connectivity checks of Android, iOS, Windows and Firefox, and anything else.
  const char* foreign[] = {"connectivitycheck.gstatic.com",
                           "clients3.google.com",
                           "captive.apple.com",
                           "www.msftconnecttest.com",
                           "www.msftncsi.com",
                           "detectportal.firefox.com",
                           "192.168.4.10",
                           "192.168.4.1.example.com",
                           "ledframe",
                           "ledframe.local.example.com",
                           "otherframe.local",
                           ""};
  for (const char* host : foreign) {
    TEST_ASSERT_FALSE_MESSAGE(ledframe::isDeviceHost(host, ap, "ledframe"), host);
  }
  TEST_ASSERT_FALSE(ledframe::isDeviceHost(nullptr, ap, "ledframe"));
  TEST_ASSERT_FALSE(ledframe::isDeviceHost(".local", ap, ""));
}

void test_parse_test_pattern() {
  ledframe::ApiError error;
  bool on = false;
  const char* start = "{\"on\":true}";
  TEST_ASSERT_TRUE(ledframe::parseTestPattern(start, std::strlen(start), on, error));
  TEST_ASSERT_TRUE(on);
  const char* stop = "{\"on\":false,\"extra\":1}";
  TEST_ASSERT_TRUE(ledframe::parseTestPattern(stop, std::strlen(stop), on, error));
  TEST_ASSERT_FALSE(on);
  const char* bad[] = {"{}", "{\"on\":1}", "{\"on\":\"yes\"}", "[]", "not json"};
  for (const char* text : bad) {
    TEST_ASSERT_FALSE_MESSAGE(ledframe::parseTestPattern(text, std::strlen(text), on, error), text);
    TEST_ASSERT_EQUAL_INT(400, error.status);
  }
}

void test_preset_paths() {
  uint8_t id = 0;
  ASSERT_ENUM(PresetRoute::kItem, ledframe::parsePresetPath("/api/presets/7", id));
  TEST_ASSERT_EQUAL_UINT8(7, id);
  ASSERT_ENUM(PresetRoute::kApply, ledframe::parsePresetPath("/api/presets/255/apply", id));
  TEST_ASSERT_EQUAL_UINT8(255, id);
  const char* invalid[] = {"/api/presets/",      "/api/presets/0",  "/api/presets/256",
                           "/api/presets/abc",   "/api/presets/7/", "/api/presets/7/run",
                           "/api/presets/00001", "/api/presetsx/1", "/api/state"};
  for (const char* path : invalid) {
    ASSERT_ENUM(PresetRoute::kInvalid, ledframe::parsePresetPath(path, id));
  }
  ASSERT_ENUM(PresetRoute::kInvalid, ledframe::parsePresetPath(nullptr, id));
}

// --- System -----------------------------------------------------------------------

void test_write_info_and_scan() {
  ledframe::SystemInfo info;
  info.version = "2.0.0-dev";
  info.uptimeS = 12;
  info.ledCount = 150;
  info.maxLeds = 1024;
  info.testPattern = true;
  info.ledPin = 4;
  info.stats.frameFps = 50;
  info.wifi.mode = ledframe::WifiMode::kAccessPoint;
  std::strcpy(info.wifi.apName, "LedFrame-F9A5");
  std::strcpy(info.wifi.ip, "192.168.4.1");
  parseOutput(ledframe::writeInfo(info, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("2.0.0-dev", doc["version"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(150, doc["leds"].as<int>());
  TEST_ASSERT_EQUAL_INT(1024, doc["maxLeds"].as<int>());
  TEST_ASSERT_TRUE(doc["testPattern"].as<bool>());
  TEST_ASSERT_EQUAL_INT(4, doc["ledPin"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(18, doc["ledPins"].size());
  TEST_ASSERT_EQUAL_INT(2, doc["ledPins"][0].as<int>());
  TEST_ASSERT_EQUAL_INT(33, doc["ledPins"][17].as<int>());
  TEST_ASSERT_EQUAL_INT(50, doc["engine"]["fps"].as<int>());
  TEST_ASSERT_EQUAL_STRING("ap", doc["wifi"]["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("LedFrame-F9A5", doc["wifi"]["apName"].as<const char*>());
  TEST_ASSERT_FALSE(doc["wifi"]["retrying"].as<bool>());
  TEST_ASSERT_TRUE(doc["wifi"]["retrying"].is<bool>());
  TEST_ASSERT_TRUE(doc["wifi"]["rssi"].isNull());
  TEST_ASSERT_TRUE(doc["resetReason"].isNull());        // No crash info given.
  TEST_ASSERT_FALSE(doc["time"]["synced"].as<bool>());  // not synchronized yet
  TEST_ASSERT_TRUE(doc["time"]["synced"].is<bool>());
  TEST_ASSERT_TRUE(doc["time"]["local"].isNull());
  TEST_ASSERT_FALSE(doc["time"]["sntp"].as<bool>());
  TEST_ASSERT_TRUE(doc["time"]["sntp"].is<bool>());

  // Synchronized clock: local time and weekday (Monday = 0).
  info.time.known = true;
  info.time.year = 2026;
  info.time.month = 11;
  info.time.day = 3;
  info.time.hour = 7;
  info.time.minute = 5;
  info.time.second = 9;
  info.time.weekday = 1;
  parseOutput(ledframe::writeInfo(info, buffer, sizeof(buffer)));
  TEST_ASSERT_TRUE(doc["time"]["synced"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("2026-11-03 07:05:09", doc["time"]["local"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, doc["time"]["weekday"].as<int>());
  info.time = ledframe::WallTime{};

  // Reset reason without a core dump.
  ledframe::CrashInfo crash;
  crash.resetReason = "power-on";
  info.crash = &crash;
  parseOutput(ledframe::writeInfo(info, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("power-on", doc["resetReason"].as<const char*>());
  TEST_ASSERT_TRUE(doc["lastCrash"].isNull());

  // A panic with a core dump.
  crash.resetReason = "panic";
  crash.hasCoredump = true;
  std::strcpy(crash.task, "render");
  crash.pc = 0x400D1234;
  crash.depth = 2;
  crash.backtrace[0] = 0x400D1234;
  crash.backtrace[1] = 0x40081ABC;
  parseOutput(ledframe::writeInfo(info, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("panic", doc["resetReason"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("render", doc["lastCrash"]["task"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("0x400D1234", doc["lastCrash"]["pc"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(2, doc["lastCrash"]["backtrace"].size());
  TEST_ASSERT_EQUAL_STRING("0x40081ABC", doc["lastCrash"]["backtrace"][1].as<const char*>());
  TEST_ASSERT_FALSE(doc["lastCrash"]["backtraceCorrupted"].as<bool>());

  ledframe::WifiNetwork networks[2];
  std::strcpy(networks[0].ssid, "Home");
  networks[0].rssi = -40;
  networks[0].secure = true;
  std::strcpy(networks[1].ssid, "Cafe \"free\"");
  parseOutput(ledframe::writeScan(networks, 2, false, buffer, sizeof(buffer)));
  TEST_ASSERT_FALSE(doc["scanning"].as<bool>());
  TEST_ASSERT_EQUAL_UINT32(2, doc["networks"].size());
  TEST_ASSERT_EQUAL_INT(-40, doc["networks"][0]["rssi"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Cafe \"free\"", doc["networks"][1]["ssid"].as<const char*>());

  parseOutput(ledframe::writeError("bad \"value\"", buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("bad \"value\"", doc["error"].as<const char*>());
}

void test_small_buffers() {
  char small[4];
  StateSnapshot state;
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writeState(state, registry(), small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writeEffects(registry(), small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writeConfig(Config{}, small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writeError("x", nullptr, 100));
}

// The web server sizes its response buffer with write(nullptr, 0), then writes into
// a buffer of exactly that size plus the terminating '\0'.
template <typename Write>
void expectSizeQuery(const char* name, Write write) {
  const size_t needed = write(nullptr, 0);
  TEST_ASSERT_TRUE_MESSAGE(needed > 0, name);
  TEST_ASSERT_TRUE_MESSAGE(needed < sizeof(buffer), name);
  std::memset(buffer, 'x', sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(needed, write(buffer, needed + 1), name);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(needed, std::strlen(buffer), name);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, write(buffer, needed), name);  // No room for '\0'.
}

void test_size_query_matches_output() {
  StateSnapshot state;
  state.effect = indexOf("level");
  state.paramCount = 4;
  expectSizeQuery("state", [&](char* out, size_t capacity) {
    return ledframe::writeState(state, registry(), out, capacity);
  });
  expectSizeQuery("effects", [](char* out, size_t capacity) {
    return ledframe::writeEffects(registry(), out, capacity);
  });
  static Palettes palettes;
  expectSizeQuery("palettes", [](char* out, size_t capacity) {
    return ledframe::writePalettes(palettes, 16, out, capacity);
  });
  Config config;
  std::strcpy(config.wifiSsid, "Cafe \"free\"");
  expectSizeQuery("config", [&](char* out, size_t capacity) {
    return ledframe::writeConfig(config, out, capacity);
  });
  Presets presets;
  const int32_t values[] = {30, 1, 2, 4};
  presets.save(ledframe::makePreset("Evening glow", indexOf("level"), values, 4, 4, 60));
  expectSizeQuery("presets", [&](char* out, size_t capacity) {
    return ledframe::writePresets(presets, registry(), out, capacity);
  });
  ledframe::SystemInfo info;
  info.version = "2.0.0";
  expectSizeQuery(
      "info", [&](char* out, size_t capacity) { return ledframe::writeInfo(info, out, capacity); });
  ledframe::WifiNetwork networks[1];
  std::strcpy(networks[0].ssid, "Home");
  expectSizeQuery("scan", [&](char* out, size_t capacity) {
    return ledframe::writeScan(networks, 1, true, out, capacity);
  });
  expectSizeQuery("error", [](char* out, size_t capacity) {
    return ledframe::writeError("bad \"value\"", out, capacity);
  });
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_state_single_field);
  RUN_TEST(test_state_empty_object);
  RUN_TEST(test_state_command_order);
  RUN_TEST(test_state_params_target_active_effect);
  RUN_TEST(test_state_color_values);
  RUN_TEST(test_state_palette_by_id);
  RUN_TEST(test_state_unknown_fields_are_ignored);
  RUN_TEST(test_state_invalid_input);
  RUN_TEST(test_write_state);
  RUN_TEST(test_write_effects_hides_hidden_and_describes_params);
  RUN_TEST(test_write_palettes);
  RUN_TEST(test_write_config_hides_secrets);
  RUN_TEST(test_config_patch_merges_fields);
  RUN_TEST(test_config_patch_time_fields);
  RUN_TEST(test_config_patch_schedule);
  RUN_TEST(test_config_patch_rejects_invalid_values);
  RUN_TEST(test_presets_json);
  RUN_TEST(test_preset_paths);
  RUN_TEST(test_parse_test_pattern);
  RUN_TEST(test_captive_portal_hosts);
  RUN_TEST(test_write_info_and_scan);
  RUN_TEST(test_small_buffers);
  RUN_TEST(test_size_query_matches_output);
  return UNITY_END();
}

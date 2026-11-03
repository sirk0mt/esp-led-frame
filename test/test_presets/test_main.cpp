#include <unity.h>

#include <cstdio>
#include <cstring>

#include "effects/EffectEngine.h"
#include "state/Presets.h"

using ledframe::Command;
using ledframe::CommandType;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectEngine;
using ledframe::EffectRegistry;
using ledframe::Frame;
using ledframe::FrameContext;
using ledframe::IFrameSink;
using ledframe::Palettes;
using ledframe::ParamKind;
using ledframe::ParamSpec;
using ledframe::Preset;
using ledframe::PresetError;
using ledframe::Presets;

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

namespace {

// --- Test effects -----------------------------------------------------------

class RedEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(CRGB(255, 0, 0));
    return true;
  }
};

class LevelEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(CRGB(static_cast<uint8_t>(ctx.params[0]), 0, 0));
    return true;
  }
};

template <typename T>
Effect* make(void* memory) {
  return new (memory) T();
}

constexpr ParamSpec kLevelParams[] = {
    {"level", "Level", ParamKind::Slider, 0, 100, 50},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

const EffectDescriptor kRed = {"red", "Red", 1, 0, nullptr, 0, nullptr, &make<RedEffect>};
const EffectDescriptor kLevel = {"level",      "Level", 2,       0,
                                 kLevelParams, 2,       nullptr, &make<LevelEffect>};

const EffectRegistry& registry() {
  static EffectRegistry r;
  static bool filled = false;
  if (!filled) {
    r.add(&kRed);
    r.add(&kLevel);
    filled = true;
  }
  return r;
}

uint8_t indexOf(const char* id) {
  return static_cast<uint8_t>(registry().indexOf(id));
}

class FakeSink : public IFrameSink {
 public:
  void show(const Frame& frame) override {
    brightness = frame.brightness;
    first = frame.canvas[0];
  }
  uint8_t brightness = 0;
  CRGB first;
};

Preset levelPreset(const char* name, int32_t level, int32_t palette, uint8_t brightness) {
  const int32_t params[] = {level, palette};
  return ledframe::makePreset(name, indexOf("level"), params, 2, palette, brightness);
}

Presets presets;
Presets loaded;
char buffer[8192];
Palettes palettes;

bool parse(const char* json) {
  return ledframe::deserializePresets(json, std::strlen(json), registry(), loaded);
}

}  // namespace

void setUp() {
  presets.clear();
}
void tearDown() {}

// --- CRUD -------------------------------------------------------------------

void test_save_creates_presets_with_new_ids() {
  PresetError error = PresetError::kFull;
  TEST_ASSERT_EQUAL_UINT8(1, presets.save(levelPreset("Warm", 10, 1, 100), &error));
  ASSERT_ENUM(PresetError::kNone, error);
  TEST_ASSERT_EQUAL_UINT8(2, presets.save(levelPreset("Cold", 20, 2, 50)));
  TEST_ASSERT_EQUAL_UINT8(2, presets.count());
  TEST_ASSERT_EQUAL_STRING("Warm", presets.at(0)->name);
  TEST_ASSERT_EQUAL_STRING("Cold", presets.find(2)->name);
  TEST_ASSERT_EQUAL_UINT8(2, presets.findByName("Cold")->id);
  TEST_ASSERT_EQUAL_INT32(20, presets.find(2)->params[0]);
  TEST_ASSERT_NULL(presets.find(3));
  TEST_ASSERT_NULL(presets.findByName("Missing"));
  TEST_ASSERT_NULL(presets.at(2));
}

void test_save_with_existing_name_overwrites() {
  presets.save(levelPreset("Warm", 10, 1, 100));
  presets.save(levelPreset("Cold", 20, 2, 50));
  TEST_ASSERT_EQUAL_UINT8(1, presets.save(levelPreset("Warm", 99, 4, 10)));
  TEST_ASSERT_EQUAL_UINT8(2, presets.count());
  TEST_ASSERT_EQUAL_INT32(99, presets.find(1)->params[0]);
  TEST_ASSERT_EQUAL_UINT8(10, presets.find(1)->brightness);
}

void test_overwrite_keeps_id_and_name() {
  presets.save(levelPreset("Warm", 10, 1, 100));
  ASSERT_ENUM(PresetError::kNone, presets.overwrite(1, levelPreset("Other", 5, 6, 7)));
  const Preset* preset = presets.find(1);
  TEST_ASSERT_EQUAL_STRING("Warm", preset->name);
  TEST_ASSERT_EQUAL_INT32(5, preset->params[0]);
  TEST_ASSERT_EQUAL_INT32(6, preset->palette);
  TEST_ASSERT_EQUAL_UINT8(7, preset->brightness);
  ASSERT_ENUM(PresetError::kNotFound, presets.overwrite(9, levelPreset("X", 1, 1, 1)));
}

void test_rename() {
  presets.save(levelPreset("Warm", 10, 1, 100));
  presets.save(levelPreset("Cold", 20, 2, 50));
  const uint32_t before = presets.version();
  ASSERT_ENUM(PresetError::kNone, presets.rename(1, "Evening glow"));
  TEST_ASSERT_TRUE(presets.version() > before);
  const Preset* preset = presets.find(1);
  TEST_ASSERT_EQUAL_STRING("Evening glow", preset->name);
  TEST_ASSERT_EQUAL_INT32(10, preset->params[0]);  // Content and id stay.
  TEST_ASSERT_EQUAL_UINT8(100, preset->brightness);
  TEST_ASSERT_NULL(presets.findByName("Warm"));
  TEST_ASSERT_EQUAL_STRING("Evening glow", presets.at(0)->name);  // Position stays.

  // The same name again: no change, no write.
  const uint32_t unchanged = presets.version();
  ASSERT_ENUM(PresetError::kNone, presets.rename(1, "Evening glow"));
  TEST_ASSERT_EQUAL_UINT32(unchanged, presets.version());

  ASSERT_ENUM(PresetError::kNameTaken, presets.rename(1, "Cold"));
  ASSERT_ENUM(PresetError::kInvalidName, presets.rename(1, ""));
  ASSERT_ENUM(PresetError::kInvalidName, presets.rename(1, "012345678901234567890123456789012"));
  ASSERT_ENUM(PresetError::kInvalidName, presets.rename(1, nullptr));
  ASSERT_ENUM(PresetError::kNotFound, presets.rename(9, "New"));
  TEST_ASSERT_EQUAL_STRING("Evening glow", presets.find(1)->name);
  TEST_ASSERT_EQUAL_UINT32(unchanged, presets.version());
}

void test_remove() {
  presets.save(levelPreset("A", 1, 0, 1));
  presets.save(levelPreset("B", 2, 0, 2));
  presets.save(levelPreset("C", 3, 0, 3));
  ASSERT_ENUM(PresetError::kNone, presets.remove(2));
  TEST_ASSERT_EQUAL_UINT8(2, presets.count());
  TEST_ASSERT_EQUAL_STRING("A", presets.at(0)->name);
  TEST_ASSERT_EQUAL_STRING("C", presets.at(1)->name);
  ASSERT_ENUM(PresetError::kNotFound, presets.remove(2));
  // Ids are not reused right away: the next preset gets 4, not 2.
  TEST_ASSERT_EQUAL_UINT8(4, presets.save(levelPreset("D", 4, 0, 4)));
}

void test_limit_of_32_presets() {
  char name[8];
  for (int i = 0; i < Presets::kMaxPresets; ++i) {
    std::snprintf(name, sizeof(name), "p%d", i);
    TEST_ASSERT_NOT_EQUAL(0, presets.save(levelPreset(name, i, 0, 1)));
  }
  PresetError error = PresetError::kNone;
  TEST_ASSERT_EQUAL_UINT8(0, presets.save(levelPreset("one-more", 1, 0, 1), &error));
  ASSERT_ENUM(PresetError::kFull, error);
  // Overwriting by name still works when the list is full.
  TEST_ASSERT_EQUAL_UINT8(1, presets.save(levelPreset("p0", 77, 0, 1)));
  TEST_ASSERT_EQUAL_UINT8(Presets::kMaxPresets, presets.count());
}

void test_invalid_names() {
  PresetError error = PresetError::kNone;
  TEST_ASSERT_EQUAL_UINT8(0, presets.save(levelPreset("", 1, 0, 1), &error));
  ASSERT_ENUM(PresetError::kInvalidName, error);
  TEST_ASSERT_FALSE(Presets::isValidName("bad\tname"));
  TEST_ASSERT_FALSE(Presets::isValidName(nullptr));
  TEST_ASSERT_FALSE(Presets::isValidName("012345678901234567890123456789012"));  // 33
  TEST_ASSERT_TRUE(Presets::isValidName("01234567890123456789012345678901"));    // 32
  TEST_ASSERT_TRUE(Presets::isValidName("Wieczór ciepły"));
  TEST_ASSERT_EQUAL_UINT8(0, presets.count());
}

void test_version_changes_with_every_change() {
  const uint32_t start = presets.version();
  presets.save(levelPreset("A", 1, 0, 1));
  TEST_ASSERT_TRUE(presets.version() > start);
  uint32_t last = presets.version();
  presets.overwrite(1, levelPreset("A", 2, 0, 1));
  TEST_ASSERT_TRUE(presets.version() > last);
  last = presets.version();
  presets.remove(1);
  TEST_ASSERT_TRUE(presets.version() > last);
  last = presets.version();
  presets.remove(1);  // Nothing to remove: no change.
  TEST_ASSERT_EQUAL_UINT32(last, presets.version());
}

void test_id_allocation_wraps_and_skips_used_ids() {
  TEST_ASSERT_TRUE(
      parse("{\"nextId\":255,\"presets\":[{\"id\":255,\"name\":\"Last\",\"effect\":\"red\"},"
            "{\"id\":1,\"name\":\"First\",\"effect\":\"red\"}]}"));
  TEST_ASSERT_EQUAL_UINT8(2, loaded.save(levelPreset("New", 1, 0, 1)));
}

// --- JSON -------------------------------------------------------------------

void test_json_round_trip() {
  presets.save(levelPreset("Warm \"glow\"", 10, 1, 100));
  presets.save(levelPreset("Cold", 20, 2, 50));
  presets.remove(1);
  presets.save(levelPreset("Red", 0, 0, 255));
  const size_t length = ledframe::serializePresets(presets, registry(), buffer, sizeof(buffer));
  TEST_ASSERT_TRUE(length > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"params\":{\"level\":20,\"palette\":2}"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"nextId\":4"));

  TEST_ASSERT_TRUE(ledframe::deserializePresets(buffer, length, registry(), loaded));
  TEST_ASSERT_EQUAL_UINT8(2, loaded.count());
  const Preset* cold = loaded.find(2);
  TEST_ASSERT_NOT_NULL(cold);
  TEST_ASSERT_EQUAL_STRING("Cold", cold->name);
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), cold->effect);
  TEST_ASSERT_EQUAL_INT32(20, cold->params[0]);
  TEST_ASSERT_EQUAL_INT32(2, cold->palette);
  TEST_ASSERT_EQUAL_UINT8(50, cold->brightness);
  TEST_ASSERT_EQUAL_UINT8(3, loaded.findByName("Red")->id);
  // nextId survives: the next preset gets 4.
  TEST_ASSERT_EQUAL_UINT8(4, loaded.save(levelPreset("Next", 1, 0, 1)));
}

void test_json_broken_file_gives_no_presets() {
  presets.save(levelPreset("A", 1, 0, 1));
  loaded = presets;
  const char* broken[] = {"", "{", "nope", "[]"};
  for (const char* text : broken) {
    TEST_ASSERT_FALSE_MESSAGE(parse(text), text);
    TEST_ASSERT_EQUAL_UINT8(0, loaded.count());
  }
}

void test_json_skips_bad_entries() {
  TEST_ASSERT_TRUE(
      parse("{\"presets\":["
            "{\"id\":1,\"name\":\"Ok\",\"effect\":\"level\",\"bri\":300,\"palette\":99,"
            "\"params\":{\"level\":500,\"glow\":1,\"palette\":\"x\"}},"
            "{\"id\":2,\"name\":\"Gone\",\"effect\":\"galaxy\"},"
            "{\"id\":1,\"name\":\"Same id\",\"effect\":\"red\"},"
            "{\"id\":3,\"name\":\"Ok\",\"effect\":\"red\"},"
            "{\"id\":0,\"name\":\"Zero\",\"effect\":\"red\"},"
            "{\"id\":4,\"name\":\"\",\"effect\":\"red\"},"
            "{\"id\":5,\"name\":\"Plain\",\"effect\":\"red\"},"
            "7]}"));
  TEST_ASSERT_EQUAL_UINT8(2, loaded.count());
  const Preset* ok = loaded.find(1);
  TEST_ASSERT_EQUAL_INT32(100, ok->params[0]);                      // Clamped.
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, ok->params[1]);  // Wrong type: default.
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, ok->palette);    // Clamped to default.
  TEST_ASSERT_EQUAL_UINT8(128, ok->brightness);                     // Out of range: default.
  TEST_ASSERT_NOT_NULL(loaded.find(5));
  // Without nextId the next id follows the highest one.
  TEST_ASSERT_EQUAL_UINT8(6, loaded.save(levelPreset("Next", 1, 0, 1)));
}

void test_json_small_buffer() {
  presets.save(levelPreset("A", 1, 0, 1));
  char small[8];
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializePresets(presets, registry(), small, 8));
}

// --- Applying a preset --------------------------------------------------------

void test_to_command() {
  const Command command = levelPreset("A", 42, 3, 77).toCommand();
  ASSERT_ENUM(CommandType::ApplyPreset, command.type);
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), command.effect);
  TEST_ASSERT_EQUAL_INT32(3, command.palette);
  TEST_ASSERT_EQUAL_UINT8(77, command.brightness);
  TEST_ASSERT_EQUAL_UINT8(2, command.paramCount);
  TEST_ASSERT_EQUAL_UINT8(1, command.params[1].index);
  TEST_ASSERT_EQUAL_INT32(3, command.params[1].value);
}

void test_apply_preset_with_transition() {
  static FakeSink sink;
  static EffectEngine engine(registry(), palettes, sink);
  engine.apply(Command::setTransition(100));
  engine.apply(Command::setEffect(indexOf("red")));
  engine.tick(0);
  TEST_ASSERT_EQUAL_UINT8(255, sink.first.r);

  TEST_ASSERT_TRUE(engine.apply(levelPreset("Dim", 20, 3, 77).toCommand()));
  TEST_ASSERT_TRUE(engine.inTransition());
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), engine.activeEffect());
  TEST_ASSERT_EQUAL_INT32(20, engine.params(indexOf("level"))[0]);
  TEST_ASSERT_EQUAL_INT32(3, engine.paletteIndex());
  TEST_ASSERT_EQUAL_UINT8(77, engine.brightness());

  engine.tick(20);  // Transition starts: still the old red.
  TEST_ASSERT_EQUAL_UINT8(255, sink.first.r);
  TEST_ASSERT_EQUAL_UINT8(77, sink.brightness);
  engine.tick(70);  // Half way between 255 and 20.
  TEST_ASSERT_TRUE(sink.first.r > 100 && sink.first.r < 180);
  TEST_ASSERT_TRUE(engine.inTransition());
  engine.tick(130);  // Done.
  TEST_ASSERT_FALSE(engine.inTransition());
  TEST_ASSERT_EQUAL_UINT8(20, sink.first.r);

  // A preset of the active effect changes its values without a transition.
  TEST_ASSERT_TRUE(engine.apply(levelPreset("Bright", 90, 3, 200).toCommand()));
  TEST_ASSERT_FALSE(engine.inTransition());
  engine.tick(150);
  TEST_ASSERT_EQUAL_UINT8(90, sink.first.r);
  TEST_ASSERT_EQUAL_UINT8(200, sink.brightness);

  // Applying the same preset again changes nothing.
  TEST_ASSERT_FALSE(engine.apply(levelPreset("Bright", 90, 3, 200).toCommand()));

  Command unknown = Command::applyPreset(200, 0, 10);
  TEST_ASSERT_FALSE(engine.apply(unknown));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_save_creates_presets_with_new_ids);
  RUN_TEST(test_save_with_existing_name_overwrites);
  RUN_TEST(test_overwrite_keeps_id_and_name);
  RUN_TEST(test_rename);
  RUN_TEST(test_remove);
  RUN_TEST(test_limit_of_32_presets);
  RUN_TEST(test_invalid_names);
  RUN_TEST(test_version_changes_with_every_change);
  RUN_TEST(test_id_allocation_wraps_and_skips_used_ids);
  RUN_TEST(test_json_round_trip);
  RUN_TEST(test_json_broken_file_gives_no_presets);
  RUN_TEST(test_json_skips_bad_entries);
  RUN_TEST(test_json_small_buffer);
  RUN_TEST(test_to_command);
  RUN_TEST(test_apply_preset_with_transition);
  return UNITY_END();
}

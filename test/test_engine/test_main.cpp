#include <unity.h>

#include "effects/EffectEngine.h"

using ledframe::Canvas;
using ledframe::Command;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectEngine;
using ledframe::EffectRegistry;
using ledframe::Frame;
using ledframe::FrameContext;
using ledframe::IFrameSink;
using ledframe::LayoutConfig;
using ledframe::Palettes;
using ledframe::ParamKind;
using ledframe::ParamSpec;

namespace {

// --- Recording of effect activity -------------------------------------------

struct Stats {
  int begins = 0;
  int renders = 0;
  int paramChanges = 0;
  uint32_t lastNowMs = 0;
  uint32_t lastDeltaMs = 0;
  uint16_t canvasSizeAtBegin = 0;
  const CRGBPalette16* lastPalette = nullptr;
  bool lastGamma = false;
  ledframe::WallTime lastTime;
};

Stats levelStats;
Stats staticStats;
Stats plainStats;
int redRenders = 0;

// --- Test effects -----------------------------------------------------------

class RedEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ++redRenders;
    ctx.canvas.fill(CRGB(255, 0, 0));
    return true;
  }
};

class BlueEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(CRGB(0, 0, 255));
    return true;
  }
};

constexpr ParamSpec kLevelParams[] = {
    {"level", "Level", ParamKind::Slider, 0, 100, 50},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

// Fills the canvas with red = level and records the frame context.
class LevelEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    ++levelStats.begins;
    levelStats.canvasSizeAtBegin = ctx.canvas.size();
  }
  bool render(FrameContext& ctx) override {
    ++levelStats.renders;
    levelStats.lastNowMs = ctx.nowMs;
    levelStats.lastDeltaMs = ctx.deltaMs;
    levelStats.lastPalette = &ctx.palette;
    levelStats.lastTime = ctx.wallTime;
    ctx.canvas.fill(CRGB(static_cast<uint8_t>(ctx.params[0]), 0, 0));
    return true;
  }
  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override {
    ++levelStats.paramChanges;
  }
};

constexpr ParamSpec kStaticParams[] = {
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0x00FF00},
};

class StaticEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ++staticStats.renders;
    staticStats.lastGamma = ctx.gamma;
    ctx.canvas.fill(CRGB(static_cast<uint32_t>(ctx.params[0])));
    return true;
  }
};

// No parameters: uses the global palette.
class PlainEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ++plainStats.renders;
    plainStats.lastPalette = &ctx.palette;
    return false;
  }
};

// Counts its constructions and destructions, so a test sees every object end.
int constructed = 0;
int destroyed = 0;

class CountedEffect : public Effect {
 public:
  CountedEffect() { ++constructed; }
  ~CountedEffect() override { ++destroyed; }
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(CRGB(0, 255, 0));
    return true;
  }
};

template <typename T>
Effect* make(void* memory) {
  return new (memory) T();
}
Effect* makeNothing(void* /*memory*/) {
  return nullptr;
}

bool failCreate = false;  // makes the "flaky" factory fail
Effect* makeFlaky(void* memory) {
  return failCreate ? nullptr : new (memory) CountedEffect();
}

const EffectDescriptor kDescriptors[] = {
    {"red", "Red", 1, 0, nullptr, 0, nullptr, &make<RedEffect>},
    {"blue", "Blue", 2, 0, nullptr, 0, nullptr, &make<BlueEffect>},
    {"level", "Level", 3, ledframe::kFlagPalette, kLevelParams, 2, nullptr, &make<LevelEffect>},
    {"static", "Static", 4, ledframe::kFlagStatic, kStaticParams, 1, nullptr, &make<StaticEffect>},
    {"plain", "Plain", 5, 0, nullptr, 0, nullptr, &make<PlainEffect>},
    {"broken", "Broken", 6, 0, nullptr, 0, nullptr, &makeNothing},
    {"counted", "Counted", 7, 0, nullptr, 0, nullptr, &make<CountedEffect>},
    {"flaky", "Flaky", 8, 0, nullptr, 0, nullptr, &makeFlaky},
};

const EffectRegistry& testRegistry() {
  static EffectRegistry registry;
  static bool filled = false;
  if (!filled) {
    for (const EffectDescriptor& descriptor : kDescriptors) {
      registry.add(&descriptor);
    }
    filled = true;
  }
  return registry;
}

uint8_t indexOf(const char* id) {
  return static_cast<uint8_t>(testRegistry().indexOf(id));
}

// --- Fake output ------------------------------------------------------------

class FakeSink : public IFrameSink {
 public:
  void show(const Frame& frame) override {
    ++shows;
    brightness = frame.brightness;
    size = frame.canvas.size();
    layoutWidth = frame.layout.width();
    first = frame.canvas[0];
  }
  int shows = 0;
  uint8_t brightness = 0;
  uint16_t size = 0;
  uint16_t layoutWidth = 0;
  CRGB first;
};

Palettes palettes;
FakeSink* sink = nullptr;
EffectEngine* engine = nullptr;

void applyOk(const Command& command) {
  TEST_ASSERT_TRUE(engine->apply(command));
}

void setLevel(uint8_t effect, int32_t level) {
  Command command = Command::setParams(effect);
  command.addParam(0, level);
  engine->apply(command);
}

}  // namespace

void setUp() {
  levelStats = Stats{};
  staticStats = Stats{};
  plainStats = Stats{};
  redRenders = 0;
  constructed = 0;
  destroyed = 0;
  failCreate = false;
  palettes.setGamma(false);  // shared by every engine of this file
  sink = new FakeSink();
  engine = new EffectEngine(testRegistry(), palettes, *sink);
}

void tearDown() {
  delete engine;
  delete sink;
}

void test_test_registry_is_complete() {
  TEST_ASSERT_EQUAL_UINT8(8, testRegistry().size());
  TEST_ASSERT_TRUE(testRegistry().ok());
}

void test_initial_state() {
  TEST_ASSERT_TRUE(engine->on());
  TEST_ASSERT_EQUAL_UINT8(128, engine->brightness());
  TEST_ASSERT_EQUAL_UINT32(500, engine->transitionMs());
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kNoEffect, engine->activeEffect());
  TEST_ASSERT_NULL(engine->activeDescriptor());
  TEST_ASSERT_EQUAL_UINT8(50, engine->fps());
  TEST_ASSERT_EQUAL_INT32(50, engine->params(indexOf("level"))[0]);  // default
  TEST_ASSERT_NULL(engine->params(EffectEngine::kNoEffect));
  // The first frame is always shown, even without an effect.
  TEST_ASSERT_TRUE(engine->tick(0));
  TEST_ASSERT_EQUAL_INT(1, sink->shows);
  TEST_ASSERT_EQUAL_UINT16(150, sink->size);
  TEST_ASSERT_FALSE(engine->tick(20));
}

void test_transition_takes_configured_time() {
  applyOk(Command::setTransition(100));
  applyOk(Command::setEffect(indexOf("red")));
  TEST_ASSERT_FALSE(engine->inTransition());  // Nothing to blend from yet.
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.r);

  applyOk(Command::setEffect(indexOf("blue")));
  TEST_ASSERT_TRUE(engine->inTransition());
  TEST_ASSERT_EQUAL_UINT8(indexOf("blue"), engine->activeEffect());

  TEST_ASSERT_TRUE(engine->tick(20));  // The transition starts with this frame.
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.r);
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.b);

  TEST_ASSERT_TRUE(engine->tick(70));  // Half way.
  TEST_ASSERT_UINT8_WITHIN(3, 128, sink->first.r);
  TEST_ASSERT_UINT8_WITHIN(3, 127, sink->first.b);
  TEST_ASSERT_TRUE(engine->inTransition());

  TEST_ASSERT_TRUE(engine->tick(110));  // 90 of 100 ms.
  TEST_ASSERT_TRUE(engine->inTransition());
  TEST_ASSERT_TRUE(sink->first.b > 200);

  TEST_ASSERT_TRUE(engine->tick(130));  // 110 ms: done.
  TEST_ASSERT_FALSE(engine->inTransition());
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.r);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);
}

void test_transition_zero_switches_immediately() {
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(indexOf("red")));
  engine->tick(0);
  applyOk(Command::setEffect(indexOf("blue")));
  TEST_ASSERT_FALSE(engine->inTransition());
  engine->tick(20);
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.r);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);
}

void test_interrupted_transition_ends_on_last_effect() {
  applyOk(Command::setTransition(100));
  applyOk(Command::setEffect(indexOf("red")));
  engine->tick(0);
  applyOk(Command::setEffect(indexOf("blue")));
  engine->tick(20);
  engine->tick(40);
  applyOk(Command::setEffect(indexOf("level")));  // Blend now goes from blue to level.
  TEST_ASSERT_TRUE(engine->inTransition());
  engine->tick(60);
  engine->tick(160);
  TEST_ASSERT_FALSE(engine->inTransition());
  TEST_ASSERT_EQUAL_UINT8(50, sink->first.r);
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.b);
}

void test_params_survive_effect_switch() {
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(indexOf("level")));
  setLevel(Command::kActiveEffect, 10);
  applyOk(Command::setEffect(indexOf("red")));
  TEST_ASSERT_EQUAL_INT32(10, engine->params(indexOf("level"))[0]);

  applyOk(Command::setEffect(indexOf("level")));
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT8(10, sink->first.r);
}

void test_params_for_inactive_effect_are_stored() {
  applyOk(Command::setEffect(indexOf("red")));
  setLevel(indexOf("level"), 33);
  TEST_ASSERT_EQUAL_INT32(33, engine->params(indexOf("level"))[0]);
  TEST_ASSERT_EQUAL_INT(0, levelStats.paramChanges);  // No instance to notify.
}

void test_param_change_notifies_active_effect() {
  applyOk(Command::setEffect(indexOf("level")));
  setLevel(Command::kActiveEffect, 20);
  TEST_ASSERT_EQUAL_INT(1, levelStats.paramChanges);
  setLevel(Command::kActiveEffect, 20);  // Same value: no change.
  TEST_ASSERT_EQUAL_INT(1, levelStats.paramChanges);
}

void test_invalid_values_are_clamped() {
  const uint8_t level = indexOf("level");
  applyOk(Command::setEffect(level));
  setLevel(level, 500);
  TEST_ASSERT_EQUAL_INT32(100, engine->params(level)[0]);
  setLevel(level, -5);
  TEST_ASSERT_EQUAL_INT32(0, engine->params(level)[0]);

  Command palette = Command::setParams(level);
  palette.addParam(1, 99);  // Palette parameter outside the list.
  engine->apply(palette);
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, engine->params(level)[1]);

  Command unknownParam = Command::setParams(level);
  unknownParam.addParam(7, 1);
  TEST_ASSERT_FALSE(engine->apply(unknownParam));

  applyOk(Command::setTransition(60000));
  TEST_ASSERT_EQUAL_UINT32(EffectEngine::kMaxTransitionMs, engine->transitionMs());

  applyOk(Command::setPalette(3));
  applyOk(Command::setPalette(-4));
  TEST_ASSERT_EQUAL_INT32(Palettes::kDefaultIndex, engine->paletteIndex());
}

void test_invalid_targets_are_ignored() {
  applyOk(Command::setEffect(indexOf("red")));
  TEST_ASSERT_FALSE(engine->apply(Command::setEffect(200)));
  TEST_ASSERT_FALSE(engine->apply(Command::setEffect(indexOf("red"))));     // Already active.
  TEST_ASSERT_FALSE(engine->apply(Command::setEffect(indexOf("broken"))));  // Factory fails.
  TEST_ASSERT_EQUAL_UINT8(indexOf("red"), engine->activeEffect());
  Command params = Command::setParams(200);
  params.addParam(0, 1);
  TEST_ASSERT_FALSE(engine->apply(params));
}

void test_static_effect_shows_one_frame_per_change() {
  applyOk(Command::setEffect(indexOf("static")));
  TEST_ASSERT_TRUE(engine->tick(0));
  TEST_ASSERT_EQUAL_INT(1, staticStats.renders);
  TEST_ASSERT_EQUAL_INT(1, sink->shows);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.g);

  TEST_ASSERT_FALSE(engine->tick(20));
  TEST_ASSERT_FALSE(engine->tick(40));
  TEST_ASSERT_EQUAL_INT(1, staticStats.renders);
  TEST_ASSERT_EQUAL_INT(1, sink->shows);

  Command color = Command::setParams();
  color.addParam(0, 0x0000FF);
  applyOk(color);
  TEST_ASSERT_TRUE(engine->tick(60));
  TEST_ASSERT_FALSE(engine->tick(80));
  TEST_ASSERT_EQUAL_INT(2, staticStats.renders);
  TEST_ASSERT_EQUAL_INT(2, sink->shows);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);

  // Brightness alone shows the frame again without rendering it.
  applyOk(Command::setBrightness(10));
  TEST_ASSERT_TRUE(engine->tick(100));
  TEST_ASSERT_EQUAL_INT(2, staticStats.renders);
  TEST_ASSERT_EQUAL_INT(3, sink->shows);
  TEST_ASSERT_EQUAL_UINT8(10, sink->brightness);
  TEST_ASSERT_FALSE(engine->apply(Command::setBrightness(10)));
}

void test_frame_schedule_follows_fps() {
  applyOk(Command::setEffect(indexOf("level")));
  engine->tick(0);
  TEST_ASSERT_EQUAL_INT(1, levelStats.renders);
  TEST_ASSERT_FALSE(engine->tick(5));
  TEST_ASSERT_FALSE(engine->tick(18));
  TEST_ASSERT_EQUAL_INT(1, levelStats.renders);
  TEST_ASSERT_TRUE(engine->tick(19));  // 1 ms early is taken (kEarlyMs).
  TEST_ASSERT_TRUE(engine->tick(41));  // A late frame keeps the cadence: next at 60.
  TEST_ASSERT_TRUE(engine->tick(60));
  TEST_ASSERT_EQUAL_INT(4, levelStats.renders);
  TEST_ASSERT_EQUAL_UINT32(60, levelStats.lastNowMs);
  TEST_ASSERT_EQUAL_UINT32(19, levelStats.lastDeltaMs);
  TEST_ASSERT_EQUAL_UINT32(80, engine->nextFrameMs());

  TEST_ASSERT_TRUE(engine->tick(500));  // After a stall the schedule restarts.
  TEST_ASSERT_EQUAL_UINT32(440, levelStats.lastDeltaMs);
  TEST_ASSERT_EQUAL_UINT32(520, engine->nextFrameMs());
  TEST_ASSERT_FALSE(engine->tick(518));
  TEST_ASSERT_TRUE(engine->tick(519));

  engine->setFps(100);
  TEST_ASSERT_TRUE(engine->tick(540));
  TEST_ASSERT_TRUE(engine->tick(550));
  engine->setFps(5);
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kMinFps, engine->fps());
  engine->setFps(200);
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kMaxFps, engine->fps());
}

// Frames an engine at `fps` renders between 0 and endMs. With pollMs > 0 tick() is
// called every pollMs; with 0 the caller sleeps until nextFrameMs() like App.
uint32_t countFrames(uint8_t fps, uint32_t endMs, uint32_t pollMs) {
  FakeSink localSink;
  EffectEngine local(testRegistry(), palettes, localSink);
  local.setFps(fps);
  local.apply(Command::setEffect(indexOf("red")));
  uint32_t nowMs = 0;
  while (nowMs < endMs) {
    local.tick(nowMs);
    const uint32_t next = pollMs > 0 ? nowMs + pollMs : local.nextFrameMs();
    nowMs = next > nowMs ? next : nowMs + 1;
  }
  return local.frames();
}

// The interval is counted in µs: 60 FPS gives 60 frames a second, not 62.5.
void test_frame_count_matches_fps() {
  for (uint32_t pollMs : {0u, 1u}) {
    TEST_ASSERT_UINT32_WITHIN(1, 500, countFrames(50, 10000, pollMs));
    TEST_ASSERT_UINT32_WITHIN(1, 600, countFrames(60, 10000, pollMs));
    TEST_ASSERT_UINT32_WITHIN(1, 1200, countFrames(120, 10000, pollMs));
    TEST_ASSERT_UINT32_WITHIN(1, 300, countFrames(30, 10000, pollMs));
  }
}

// A caller whose clock runs 1 ms ahead of the engine's schedule (another clock,
// timer rounding) wakes 1 ms early every time and still gets every frame.
void test_tick_one_ms_early_keeps_every_frame() {
  applyOk(Command::setEffect(indexOf("red")));
  engine->tick(0);
  for (int frame = 1; frame <= 50; ++frame) {
    const uint32_t due = engine->nextFrameMs();
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(frame) * 20, due);  // No drift.
    TEST_ASSERT_TRUE(engine->tick(due - 1));
  }
  TEST_ASSERT_EQUAL_UINT32(51, engine->frames());
}

// After a long stall the engine renders one frame and then follows the normal
// schedule from there, without a burst of missed frames.
void test_long_stall_gives_no_burst() {
  applyOk(Command::setEffect(indexOf("red")));
  engine->tick(0);
  engine->tick(20);
  TEST_ASSERT_TRUE(engine->tick(5000));
  const uint32_t before = engine->frames();
  for (uint32_t nowMs = 5001; nowMs <= 5100; ++nowMs) {
    engine->tick(nowMs);
  }
  TEST_ASSERT_EQUAL_UINT32(5, engine->frames() - before);  // 5019, 5039, ... 5099
}

// Gamma correction reaches every FrameContext, draws a static effect again
// and gives effects the corrected user palette.
void test_gamma_reaches_effects() {
  applyOk(Command::setEffect(indexOf("static")));
  engine->tick(0);
  engine->tick(20);
  TEST_ASSERT_EQUAL_INT(1, staticStats.renders);  // static: drawn once
  TEST_ASSERT_FALSE(staticStats.lastGamma);

  engine->setGamma(true);
  TEST_ASSERT_TRUE(engine->gamma());
  TEST_ASSERT_TRUE(engine->tick(40));
  TEST_ASSERT_EQUAL_INT(2, staticStats.renders);
  TEST_ASSERT_TRUE(staticStats.lastGamma);
  engine->setGamma(true);  // unchanged: no new frame
  TEST_ASSERT_FALSE(engine->tick(60));
  TEST_ASSERT_EQUAL_INT(2, staticStats.renders);

  applyOk(Command::setEffect(indexOf("level")));
  applyOk(Command::setPalette(Palettes::kUserIndex));
  engine->tick(80);
  TEST_ASSERT_TRUE(levelStats.lastPalette == &palettes.get(Palettes::kUserIndex));
  engine->setGamma(false);
  engine->tick(100);
  TEST_ASSERT_TRUE(levelStats.lastPalette == &palettes.get(Palettes::kUserIndex));
  TEST_ASSERT_FALSE(engine->gamma());
}

// Wall time: unknown by default, in a FrameContext built by hand too, and the
// time set on the engine reaches the effects from the next frame.
void test_wall_time_reaches_effects() {
  ledframe::Canvas canvas;
  int32_t params[ledframe::kMaxParams] = {};
  const ledframe::FrameContext plain{canvas, 0, 0, params, palettes.get(0)};
  TEST_ASSERT_FALSE(plain.wallTime.known);
  TEST_ASSERT_FALSE(engine->wallTime().known);

  applyOk(Command::setEffect(indexOf("level")));
  engine->tick(0);
  TEST_ASSERT_FALSE(levelStats.lastTime.known);
  ledframe::WallTime time;
  time.known = true;
  time.hour = 21;
  time.minute = 30;
  time.weekday = 6;
  engine->setWallTime(time);
  engine->tick(20);
  TEST_ASSERT_TRUE(levelStats.lastTime.known);
  TEST_ASSERT_EQUAL_UINT8(21, levelStats.lastTime.hour);
  TEST_ASSERT_EQUAL_UINT16(21 * 60 + 30, levelStats.lastTime.minuteOfDay());
  TEST_ASSERT_EQUAL_UINT8(6, levelStats.lastTime.weekday);
}

void test_power_off_shows_black_once() {
  applyOk(Command::setEffect(indexOf("red")));
  engine->tick(0);
  const int rendersBefore = redRenders;
  applyOk(Command::setPower(false));
  TEST_ASSERT_FALSE(engine->apply(Command::setPower(false)));
  TEST_ASSERT_TRUE(engine->tick(20));
  TEST_ASSERT_EQUAL_UINT8(0, sink->brightness);
  TEST_ASSERT_FALSE(engine->tick(40));
  TEST_ASSERT_EQUAL_INT(rendersBefore, redRenders);  // Effects do not run while off.

  // Effect changes while off take effect without a transition.
  applyOk(Command::setEffect(indexOf("blue")));
  TEST_ASSERT_FALSE(engine->inTransition());

  applyOk(Command::setPower(true));
  TEST_ASSERT_TRUE(engine->tick(60));
  TEST_ASSERT_EQUAL_UINT8(128, sink->brightness);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);
}

void test_palette_reaches_frame_context() {
  const uint8_t level = indexOf("level");
  applyOk(Command::setEffect(level));
  applyOk(Command::setPalette(2));
  TEST_ASSERT_EQUAL_INT32(2, engine->params(level)[1]);  // Stored in the Palette param.
  TEST_ASSERT_EQUAL_INT32(2, engine->paletteIndex());
  engine->tick(0);
  TEST_ASSERT_EQUAL_PTR(&palettes.get(2), levelStats.lastPalette);

  // An effect without a Palette parameter uses the global palette.
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(indexOf("plain")));
  applyOk(Command::setPalette(5));
  engine->tick(20);
  TEST_ASSERT_EQUAL_PTR(&palettes.get(5), plainStats.lastPalette);
  TEST_ASSERT_EQUAL_INT32(5, engine->paletteIndex());
  TEST_ASSERT_EQUAL_INT32(2, engine->params(level)[1]);  // Level keeps its own palette.
}

void test_set_layout_resizes_and_restarts_effect() {
  applyOk(Command::setEffect(indexOf("level")));
  TEST_ASSERT_EQUAL_INT(1, levelStats.begins);
  TEST_ASSERT_EQUAL_UINT16(150, levelStats.canvasSizeAtBegin);

  LayoutConfig config;
  config.width = 4;
  config.height = 4;
  config.serpentine = true;
  applyOk(Command::setLayout(config));
  TEST_ASSERT_EQUAL_INT(2, levelStats.begins);
  TEST_ASSERT_EQUAL_UINT16(16, levelStats.canvasSizeAtBegin);
  TEST_ASSERT_EQUAL_UINT8(indexOf("level"), engine->activeEffect());
  TEST_ASSERT_TRUE(engine->layout().config().serpentine);
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT16(16, sink->size);
  TEST_ASSERT_EQUAL_UINT16(4, sink->layoutWidth);

  LayoutConfig invalid;
  invalid.width = 0;
  TEST_ASSERT_FALSE(engine->apply(Command::setLayout(invalid)));
  TEST_ASSERT_EQUAL_UINT16(16, engine->output().size());
  TEST_ASSERT_EQUAL_INT(2, levelStats.begins);
}

// Every effect object is destroyed exactly once: after a switch without a
// transition, at the end of a transition, when a transition is interrupted and
// with the engine.
void test_effect_objects_end_in_every_switch() {
  const uint8_t counted = indexOf("counted");
  const uint8_t red = indexOf("red");
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(counted));
  applyOk(Command::setEffect(red));
  TEST_ASSERT_EQUAL_INT(1, constructed);
  TEST_ASSERT_EQUAL_INT(1, destroyed);

  applyOk(Command::setTransition(100));
  applyOk(Command::setEffect(counted));
  applyOk(Command::setEffect(red));  // counted keeps running during the blend
  TEST_ASSERT_EQUAL_INT(2, constructed);
  TEST_ASSERT_EQUAL_INT(1, destroyed);
  engine->tick(0);
  engine->tick(20);
  TEST_ASSERT_EQUAL_INT(1, destroyed);
  engine->tick(120);  // The transition ends.
  TEST_ASSERT_FALSE(engine->inTransition());
  TEST_ASSERT_EQUAL_INT(2, destroyed);

  applyOk(Command::setEffect(counted));
  engine->tick(140);
  applyOk(Command::setEffect(red));      // blend counted -> red
  applyOk(Command::setEffect(counted));  // interrupted: the oldest (counted) ends
  TEST_ASSERT_EQUAL_INT(4, constructed);
  TEST_ASSERT_EQUAL_INT(3, destroyed);

  delete engine;
  engine = nullptr;
  TEST_ASSERT_EQUAL_INT(4, destroyed);
  engine = new EffectEngine(testRegistry(), palettes, *sink);
}

// A layout change creates the effect again on the new canvas. When that fails, the
// user's effect stays active (so the saved state keeps it) and choosing it retries.
void test_set_layout_keeps_effect_when_recreation_fails() {
  const uint8_t flaky = indexOf("flaky");
  applyOk(Command::setEffect(flaky));
  LayoutConfig config;
  config.width = 4;
  config.height = 4;
  applyOk(Command::setLayout(config));
  TEST_ASSERT_EQUAL_UINT8(flaky, engine->activeEffect());
  TEST_ASSERT_EQUAL_INT(2, constructed);
  TEST_ASSERT_EQUAL_INT(1, destroyed);

  failCreate = true;
  config.width = 5;
  applyOk(Command::setLayout(config));
  TEST_ASSERT_EQUAL_UINT8(flaky, engine->activeEffect());
  TEST_ASSERT_EQUAL_INT(2, destroyed);
  engine->tick(0);  // Nothing to draw, but nothing breaks either.
  TEST_ASSERT_EQUAL_UINT16(20, sink->size);

  failCreate = false;
  applyOk(Command::setEffect(flaky));
  TEST_ASSERT_EQUAL_INT(3, constructed);
  engine->tick(20);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.g);
}

void test_override_keeps_user_effect_active() {
  const uint8_t level = indexOf("level");
  const uint8_t red = indexOf("red");
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(level));
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT8(50, sink->first.r);

  applyOk(Command::setOverride(red));
  TEST_ASSERT_FALSE(engine->apply(Command::setOverride(red)));  // Already shown.
  TEST_ASSERT_EQUAL_UINT8(level, engine->activeEffect());       // Saved state keeps level.
  TEST_ASSERT_EQUAL_UINT8(red, engine->overrideEffect());
  TEST_ASSERT_EQUAL_PTR(engine->activeDescriptor(), testRegistry().at(level));
  engine->tick(20);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.r);

  // Parameters of the active effect still go to the user's effect.
  setLevel(Command::kActiveEffect, 30);
  TEST_ASSERT_EQUAL_INT32(30, engine->params(level)[0]);

  applyOk(Command::setOverride(Command::kNoOverride));
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kNoEffect, engine->overrideEffect());
  TEST_ASSERT_EQUAL_UINT8(level, engine->activeEffect());
  engine->tick(40);
  TEST_ASSERT_EQUAL_UINT8(30, sink->first.r);
}

void test_set_effect_ends_override() {
  applyOk(Command::setTransition(0));
  applyOk(Command::setEffect(indexOf("level")));
  applyOk(Command::setOverride(indexOf("red")));
  applyOk(Command::setEffect(indexOf("blue")));
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kNoEffect, engine->overrideEffect());
  TEST_ASSERT_EQUAL_UINT8(indexOf("blue"), engine->activeEffect());
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);

  // Choosing the effect hidden under the override also ends it.
  applyOk(Command::setOverride(indexOf("red")));
  applyOk(Command::setEffect(indexOf("blue")));
  engine->tick(20);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.b);
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.r);
}

void test_override_without_user_effect() {
  applyOk(Command::setOverride(indexOf("red")));
  TEST_ASSERT_EQUAL_UINT8(EffectEngine::kNoEffect, engine->activeEffect());
  engine->tick(0);
  TEST_ASSERT_EQUAL_UINT8(255, sink->first.r);
  applyOk(Command::setOverride(Command::kNoOverride));
  TEST_ASSERT_TRUE(engine->tick(20));
  TEST_ASSERT_EQUAL_UINT8(0, sink->first.r);  // Nothing to return to: black.
  TEST_ASSERT_FALSE(engine->apply(Command::setOverride(200)));
}

void test_command_add_param_limit() {
  Command command = Command::setParams();
  for (uint8_t i = 0; i < ledframe::kMaxParams; ++i) {
    TEST_ASSERT_TRUE(command.addParam(i, i));
  }
  TEST_ASSERT_FALSE(command.addParam(0, 0));
  TEST_ASSERT_EQUAL_UINT8(ledframe::kMaxParams, command.paramCount);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_test_registry_is_complete);
  RUN_TEST(test_initial_state);
  RUN_TEST(test_transition_takes_configured_time);
  RUN_TEST(test_wall_time_reaches_effects);
  RUN_TEST(test_transition_zero_switches_immediately);
  RUN_TEST(test_interrupted_transition_ends_on_last_effect);
  RUN_TEST(test_params_survive_effect_switch);
  RUN_TEST(test_params_for_inactive_effect_are_stored);
  RUN_TEST(test_param_change_notifies_active_effect);
  RUN_TEST(test_invalid_values_are_clamped);
  RUN_TEST(test_invalid_targets_are_ignored);
  RUN_TEST(test_static_effect_shows_one_frame_per_change);
  RUN_TEST(test_frame_schedule_follows_fps);
  RUN_TEST(test_frame_count_matches_fps);
  RUN_TEST(test_tick_one_ms_early_keeps_every_frame);
  RUN_TEST(test_long_stall_gives_no_burst);
  RUN_TEST(test_gamma_reaches_effects);
  RUN_TEST(test_power_off_shows_black_once);
  RUN_TEST(test_palette_reaches_frame_context);
  RUN_TEST(test_set_layout_resizes_and_restarts_effect);
  RUN_TEST(test_effect_objects_end_in_every_switch);
  RUN_TEST(test_set_layout_keeps_effect_when_recreation_fails);
  RUN_TEST(test_override_keeps_user_effect_active);
  RUN_TEST(test_set_effect_ends_override);
  RUN_TEST(test_override_without_user_effect);
  RUN_TEST(test_command_add_param_limit);
  return UNITY_END();
}

#include <lib8tion.h>
#include <unity.h>

#include <vector>

#include "effects/EffectRegistry.h"
#include "render/Palettes.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::Palettes;

namespace {

enum P : uint8_t { kDensity, kDamping, kPalette };

Canvas canvas;
Palettes palettes;
// Black to white: the brightness of a pixel follows the water height.
CRGBPalette16 gray;

// One ripples effect with its own parameter values on the gray palette.
struct Ripples {
  Ripples() {
    random16_set_seed(1234);
    descriptor = EffectRegistry::global().find("ripples");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    canvas.clear();  // like the engine: begin() sees a clear canvas
    FrameContext ctx{canvas, 0, 0, params, gray};
    effect->begin(ctx);
  }

  bool render(uint32_t nowMs, uint32_t deltaMs) {
    FrameContext ctx{canvas, nowMs, deltaMs, params, gray};
    return effect->render(ctx);
  }

  // Renders 20 ms frames from the last time up to nowMs.
  void runTo(uint32_t nowMs) {
    while (timeMs + 20 <= nowMs) {
      timeMs += 20;
      render(timeMs, 20);
    }
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
  uint32_t timeMs = 0;
};

// Brightness of calm water: the gray palette at index 128.
int calmLevel() {
  return ColorFromPalette(gray, 128, 255, LINEARBLEND_NOWRAP).g;
}

// Wave energy seen on the canvas: the sum of squared differences from calm water.
long energy() {
  const int calm = calmLevel();
  long sum = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    const int d = canvas[i].g - calm;
    sum += d * d;
  }
  return sum;
}

int disturbed() {
  const int calm = calmLevel();
  int count = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    count += canvas[i].g != calm ? 1 : 0;
  }
  return count;
}

std::vector<uint32_t> pixels() {
  std::vector<uint32_t> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back((canvas[i].r << 16) | (canvas[i].g << 8) | canvas[i].b);
  }
  return out;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t level = static_cast<uint8_t>(i * 17);
    gray.entries[i] = CRGB(level, level, level);
  }
}
void tearDown() {}

void test_ripples_is_registered() {
  const EffectDescriptor* ripples = EffectRegistry::global().find("ripples");
  TEST_ASSERT_NOT_NULL(ripples);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(3, ripples->paramCount);
  TEST_ASSERT_EQUAL_INT32(2, ripples->params[kPalette].def);  // ocean
}

// Without drops the water is flat: one frame in the calm color, then nothing to send.
void test_ripples_calm_water() {
  Ripples ripples;
  ripples.params[kDensity] = 0;
  TEST_ASSERT_TRUE(ripples.render(0, 0));
  TEST_ASSERT_EQUAL_INT(0, disturbed());
  TEST_ASSERT_FALSE(ripples.render(20, 20));
  ripples.runTo(3000);
  TEST_ASSERT_EQUAL_INT(0, disturbed());
}

// A drop makes a soft dent that spreads out as a ring over many pixels.
void test_ripples_drops_spread() {
  Ripples ripples;
  ripples.params[kDensity] = 30;  // one drop per 2 s on average
  int first = 0;
  while (first == 0 && ripples.timeMs < 20000) {
    ripples.runTo(ripples.timeMs + 20);
    first = disturbed();
  }
  TEST_ASSERT_TRUE(first > 0 && first <= 9);  // one soft 3 x 3 dent fading in
  ripples.runTo(ripples.timeMs + 240);        // 6 steps later
  TEST_ASSERT_TRUE(disturbed() > 20);
}

// Without new drops the wave energy goes down: halved within 1.5 s, and after 10 s
// the water is flat again and the effect stops sending frames.
void test_ripples_energy_decreases_without_drops() {
  Ripples ripples;
  ripples.params[kDensity] = 240;  // 4 drops per second
  ripples.runTo(3000);
  ripples.params[kDensity] = 0;
  ripples.runTo(3200);  // the last drop spreads
  const long start = energy();
  TEST_ASSERT_TRUE(start > 0);
  long previousWindow = start * 2;
  for (uint32_t window = 0; window < 5; ++window) {
    long sum = 0;
    for (int frame = 0; frame < 15; ++frame) {
      ripples.runTo(ripples.timeMs + 20);
      sum += energy();
    }
    const long average = sum / 15;
    TEST_ASSERT_TRUE(average < previousWindow);  // every 0.3 s less than before
    previousWindow = average;
  }
  TEST_ASSERT_TRUE(previousWindow < start / 2);
  ripples.runTo(14000);
  TEST_ASSERT_EQUAL_INT(0, disturbed());
  TEST_ASSERT_FALSE(ripples.render(14020, 20));
}

// Stronger damping calms the water sooner.
void test_ripples_damping() {
  long left[2] = {0, 0};
  const int32_t damping[2] = {2, 32};
  for (int k = 0; k < 2; ++k) {
    Ripples ripples;
    ripples.params[kDensity] = 240;
    ripples.params[kDamping] = damping[k];
    ripples.runTo(2000);
    ripples.params[kDensity] = 0;
    ripples.runTo(3000);
    left[k] = energy();
  }
  TEST_ASSERT_TRUE(left[1] < left[0]);
}

// The same time gives the same picture at any FPS: the steps are fixed and the
// picture is interpolated by the time since the last step.
void test_ripples_does_not_depend_on_fps() {
  Ripples a;
  a.params[kDensity] = 120;
  a.render(0, 0);
  for (uint32_t ms = 10; ms <= 3000; ms += 10) {
    a.render(ms, 10);
  }
  const std::vector<uint32_t> at10ms = pixels();
  Ripples b;
  b.params[kDensity] = 120;
  b.render(0, 0);
  for (uint32_t ms = 50; ms <= 3000; ms += 50) {
    b.render(ms, 50);
  }
  TEST_ASSERT_TRUE(at10ms == pixels());
}

void test_ripples_on_extreme_layouts() {
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Ripples dot;
  dot.params[kDensity] = 240;
  dot.runTo(3000);
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  Ripples row;
  row.params[kDensity] = 240;
  row.runTo(3000);
  TEST_ASSERT_TRUE(disturbed() > 0);
  TEST_ASSERT_TRUE(canvas.resize(1, ledframe::kMaxLeds));
  Ripples column;
  column.params[kDensity] = 240;
  column.runTo(3000);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ripples_is_registered);
  RUN_TEST(test_ripples_calm_water);
  RUN_TEST(test_ripples_drops_spread);
  RUN_TEST(test_ripples_energy_decreases_without_drops);
  RUN_TEST(test_ripples_damping);
  RUN_TEST(test_ripples_does_not_depend_on_fps);
  RUN_TEST(test_ripples_on_extreme_layouts);
  return UNITY_END();
}

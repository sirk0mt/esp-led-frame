#include <unity.h>

#include <set>
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

enum P : uint8_t { kSpeed, kTwist, kDirection, kPalette };

Canvas canvas;
Palettes palettes;

// One swirl with its own parameter values: defaults, then the given changes.
struct Swirl {
  Swirl() {
    descriptor = EffectRegistry::global().find("swirl");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs) {
    return FrameContext{canvas, nowMs, 20, params, palettes.get(params[kPalette])};
  }
  bool render(uint32_t nowMs) {
    FrameContext ctx = context(nowMs);
    return effect->render(ctx);
  }
  void change(uint8_t index, int32_t value) {
    params[index] = value;
    FrameContext ctx = context(0);
    effect->onParamChanged(index, ctx);
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

uint32_t rgb(int x, int y) {
  const CRGB& c = canvas.at(x, y);
  return (c.r << 16) | (c.g << 8) | c.b;
}

std::vector<uint32_t> frameAt(Swirl& swirl, uint32_t nowMs) {
  swirl.render(nowMs);
  std::vector<uint32_t> out;
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      out.push_back(rgb(x, y));
    }
  }
  return out;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_swirl_is_registered() {
  const EffectDescriptor* swirl = EffectRegistry::global().find("swirl");
  TEST_ASSERT_NOT_NULL(swirl);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, swirl->paramCount);
  TEST_ASSERT_EQUAL_INT(kDirection, ledframe::findParam(*swirl, "direction"));
}

// Without twist the color depends on the angle only: the pixels straight above the
// center (x = 7 on 15 columns) share one color, and the colors change around it.
void test_swirl_without_twist_follows_the_angle() {
  Swirl swirl;
  swirl.change(kTwist, 0);
  swirl.render(0);
  TEST_ASSERT_EQUAL_UINT32(rgb(7, 0), rgb(7, 1));
  TEST_ASSERT_EQUAL_UINT32(rgb(7, 1), rgb(7, 2));
  TEST_ASSERT_EQUAL_UINT32(rgb(7, 9), rgb(7, 7));
  std::set<uint32_t> around = {rgb(7, 0), rgb(14, 4), rgb(7, 9), rgb(0, 4)};
  TEST_ASSERT_EQUAL_UINT32(4, around.size());
}

// With twist the same ray changes color with the distance: a spiral.
void test_swirl_twist_makes_a_spiral() {
  Swirl swirl;
  swirl.change(kTwist, 8);
  swirl.render(0);
  TEST_ASSERT_FALSE(rgb(7, 0) == rgb(7, 3));
}

// Clockwise turning subtracts the phase, counterclockwise adds it. At speed 128 the
// phase makes a full turn in 2 s, so clockwise at 0.5 s (a quarter turn) shows the
// same picture as counterclockwise at 1.5 s (three quarters).
void test_swirl_directions_are_opposite() {
  Swirl clockwise;
  clockwise.change(kSpeed, 128);
  Swirl counter;
  counter.change(kSpeed, 128);
  counter.change(kDirection, 1);
  TEST_ASSERT_TRUE(frameAt(clockwise, 500) == frameAt(counter, 1500));
  TEST_ASSERT_FALSE(frameAt(clockwise, 500) == frameAt(counter, 500));
}

// Clockwise on screen (y down): after a quarter turn the color that was right of the
// center is below it. On 15 x 9 the center (7, 4) is a pixel, so the pixel to its
// right has angle 0 and the one below a quarter turn exactly.
void test_swirl_clockwise_on_screen() {
  TEST_ASSERT_TRUE(canvas.resize(15, 9));
  Swirl swirl;
  swirl.change(kTwist, 0);
  swirl.change(kSpeed, 128);
  swirl.render(0);
  const uint32_t right = rgb(14, 4);
  swirl.render(500);  // a quarter turn later
  TEST_ASSERT_EQUAL_UINT32(right, rgb(7, 8));
  TEST_ASSERT_FALSE(right == rgb(7, 0));  // above: three quarters away
}

void test_swirl_speed_0_is_still() {
  Swirl swirl;
  swirl.change(kSpeed, 0);
  TEST_ASSERT_TRUE(swirl.render(0));
  const std::vector<uint32_t> still = frameAt(swirl, 0);
  TEST_ASSERT_FALSE(swirl.render(5000));
  swirl.change(kTwist, 20);
  TEST_ASSERT_TRUE(swirl.render(5000));
  TEST_ASSERT_FALSE(still == frameAt(swirl, 5000));
}

void test_swirl_on_extreme_layouts() {
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Swirl dot;
  dot.render(1000);
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  Swirl row;
  row.render(1000);
  TEST_ASSERT_TRUE(canvas.resize(1, ledframe::kMaxLeds));
  Swirl column;
  column.render(1000);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_swirl_is_registered);
  RUN_TEST(test_swirl_without_twist_follows_the_angle);
  RUN_TEST(test_swirl_twist_makes_a_spiral);
  RUN_TEST(test_swirl_directions_are_opposite);
  RUN_TEST(test_swirl_clockwise_on_screen);
  RUN_TEST(test_swirl_speed_0_is_still);
  RUN_TEST(test_swirl_on_extreme_layouts);
  return UNITY_END();
}

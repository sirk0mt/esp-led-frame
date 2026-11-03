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

enum P : uint8_t { kSpeed, kDensity, kDirection, kPalette };

Canvas canvas;
Palettes palettes;

// One radial rainbow with defaults; begin() runs on the current canvas size.
struct Radial {
  Radial() {
    descriptor = EffectRegistry::global().find("radial-rainbow");
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

uint32_t at(int x, int y) {
  const CRGB& c = canvas.at(x, y);
  return (c.r << 16) | (c.g << 8) | c.b;
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
}
void tearDown() {}

void test_radial_rainbow_is_registered() {
  const EffectDescriptor* radial = EffectRegistry::global().find("radial-rainbow");
  TEST_ASSERT_NOT_NULL(radial);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, radial->paramCount);
  TEST_ASSERT_EQUAL_INT(kDirection, ledframe::findParam(*radial, "direction"));
  TEST_ASSERT_EQUAL_STRING("outward|inward", radial->params[kDirection].options);
}

void test_rings_are_symmetric_around_the_center() {
  Radial radial;
  radial.render(1234);
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 15; ++x) {
      TEST_ASSERT_EQUAL_HEX32(at(x, y), at(14 - x, y));      // left-right
      TEST_ASSERT_EQUAL_HEX32(at(x, y), at(x, 9 - y));       // top-bottom
    }
  }
  TEST_ASSERT_TRUE(at(7, 4) != at(0, 0));  // center and corner differ
}

void test_rings_move_outward_and_inward() {
  // 15 x 9 has its center on pixel (7, 4), so (7 + k, 4) is k pixels away. Density
  // 16 is 16 palette steps per pixel, and speed 64 moves 16 steps in 250 ms: one
  // pixel.
  TEST_ASSERT_TRUE(canvas.resize(15, 9));
  Radial out;
  out.params[kDensity] = 16;
  out.params[kSpeed] = 64;
  out.render(1000);
  const uint32_t center = at(7, 4);
  const uint32_t next = at(8, 4);
  out.render(1250);
  TEST_ASSERT_EQUAL_HEX32(center, at(8, 4));  // the center color moved one pixel out

  Radial in;
  in.params[kDensity] = 16;
  in.params[kSpeed] = 64;
  in.change(kDirection, 1);  // inward
  in.render(1000);
  const uint32_t ring = at(8, 4);
  in.render(1250);
  TEST_ASSERT_EQUAL_HEX32(ring, at(7, 4));  // the ring moved one pixel in
  TEST_ASSERT_TRUE(next != center);
}

void test_same_time_gives_the_same_picture() {
  Radial first;
  first.render(98765);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Radial second;
  second.render(98765);
  TEST_ASSERT_TRUE(a == pixels());
  // Long uptimes do not overflow: 2^32 ms is about 50 days.
  TEST_ASSERT_TRUE(second.render(4000000000u));
}

void test_speed_0_is_still() {
  Radial radial;
  radial.change(kSpeed, 0);
  TEST_ASSERT_TRUE(radial.render(0));
  TEST_ASSERT_FALSE(radial.render(5000));
  radial.change(kDensity, 30);  // a change draws once more
  TEST_ASSERT_TRUE(radial.render(6000));
  TEST_ASSERT_FALSE(radial.render(7000));
}

void test_strip_layouts_work() {
  TEST_ASSERT_TRUE(canvas.resize(1024, 1));  // the largest distance from the center
  Radial wide;
  TEST_ASSERT_TRUE(wide.render(500));
  TEST_ASSERT_EQUAL_HEX32(at(0, 0), at(1023, 0));
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Radial dot;
  TEST_ASSERT_TRUE(dot.render(500));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_radial_rainbow_is_registered);
  RUN_TEST(test_rings_are_symmetric_around_the_center);
  RUN_TEST(test_rings_move_outward_and_inward);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_speed_0_is_still);
  RUN_TEST(test_strip_layouts_work);
  return UNITY_END();
}

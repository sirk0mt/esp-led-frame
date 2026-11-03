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

enum P : uint8_t { kSpeed, kScale, kPalette };

Canvas canvas;
Palettes palettes;

// One plasma with its own parameter values: defaults, then the given changes.
struct Plasma {
  Plasma() {
    descriptor = EffectRegistry::global().find("plasma");
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

std::vector<uint32_t> pixels() {
  std::vector<uint32_t> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back((canvas[i].r << 16) | (canvas[i].g << 8) | canvas[i].b);
  }
  return out;
}

std::vector<uint32_t> frameAt(Plasma& plasma, uint32_t nowMs) {
  plasma.render(nowMs);
  return pixels();
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_plasma_is_registered() {
  const EffectDescriptor* plasma = EffectRegistry::global().find("plasma");
  TEST_ASSERT_NOT_NULL(plasma);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(3, plasma->paramCount);
  TEST_ASSERT_EQUAL_INT(kScale, ledframe::findParam(*plasma, "scale"));
  TEST_ASSERT_EQUAL_INT32(1, plasma->params[kPalette].def);  // party
  TEST_ASSERT_TRUE((plasma->flags & ledframe::kFlagPalette) != 0);
}

// The same time and parameters give the same picture.
void test_plasma_is_deterministic() {
  Plasma a;
  Plasma b;
  TEST_ASSERT_TRUE(frameAt(a, 1234) == frameAt(b, 1234));
}

// Crossing waves give many colors, not stripes or a single color.
void test_plasma_has_many_colors() {
  Plasma plasma;
  const std::vector<uint32_t> frame = frameAt(plasma, 0);
  const std::set<uint32_t> colors(frame.begin(), frame.end());
  TEST_ASSERT_TRUE(colors.size() >= 30);
  // Neither rows nor columns repeat: the diagonal wave breaks the symmetry.
  TEST_ASSERT_FALSE(canvas.at(0, 0) == canvas.at(0, 1) && canvas.at(1, 0) == canvas.at(1, 1));
}

void test_plasma_moves_with_time() {
  Plasma plasma;
  const std::vector<uint32_t> start = frameAt(plasma, 0);
  TEST_ASSERT_TRUE(start != frameAt(plasma, 500));
  TEST_ASSERT_TRUE(frameAt(plasma, 500) != frameAt(plasma, 1000));
}

// Speed 0 draws once after a change and then reports unchanged frames.
void test_plasma_speed_0_is_still() {
  Plasma plasma;
  plasma.change(kSpeed, 0);
  TEST_ASSERT_TRUE(plasma.render(0));
  const std::vector<uint32_t> still = pixels();
  TEST_ASSERT_FALSE(plasma.render(5000));
  TEST_ASSERT_TRUE(still == pixels());
  plasma.change(kScale, 40);  // A parameter change draws again.
  TEST_ASSERT_TRUE(plasma.render(5000));
}

void test_plasma_scale_and_palette_change_the_picture() {
  Plasma plasma;
  const std::vector<uint32_t> base = frameAt(plasma, 700);
  plasma.change(kScale, 4);
  TEST_ASSERT_TRUE(base != frameAt(plasma, 700));
  plasma.change(kScale, 24);
  plasma.change(kPalette, 2);  // ocean
  TEST_ASSERT_TRUE(base != frameAt(plasma, 700));
}

// Hours of uptime: nowMs * speed does not overflow, and the picture still moves.
void test_plasma_runs_after_a_long_uptime() {
  Plasma plasma;
  plasma.change(kSpeed, 255);
  const uint32_t late = 4000000000u;
  TEST_ASSERT_TRUE(frameAt(plasma, late) != frameAt(plasma, late + 40));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_plasma_is_registered);
  RUN_TEST(test_plasma_is_deterministic);
  RUN_TEST(test_plasma_has_many_colors);
  RUN_TEST(test_plasma_moves_with_time);
  RUN_TEST(test_plasma_speed_0_is_still);
  RUN_TEST(test_plasma_scale_and_palette_change_the_picture);
  RUN_TEST(test_plasma_runs_after_a_long_uptime);
  return UNITY_END();
}

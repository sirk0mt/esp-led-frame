#include <unity.h>

#include "effects/EffectRegistry.h"
#include "render/Palettes.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::Palettes;

namespace {

enum P : uint8_t { kColors, kColor, kPeriod, kMinLevel, kPalette };

Canvas canvas;
Palettes palettes;

// One breathe effect with its own parameter values: defaults, then the given changes.
struct Breathe {
  Breathe() {
    descriptor = EffectRegistry::global().find("breathe");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs) {
    return FrameContext{canvas, nowMs, 20, params, palettes.get(params[kPalette]), gamma};
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
  bool gamma = false;
};

// Renders and returns the color, checking that the whole canvas has it.
CRGB colorAt(Breathe& breathe, uint32_t nowMs) {
  breathe.render(nowMs);
  for (int i = 1; i < canvas.size(); ++i) {
    TEST_ASSERT_TRUE(canvas[i] == canvas[0]);
  }
  return canvas[0];
}

// White breath: every channel is the level.
Breathe& white(Breathe& breathe) {
  breathe.change(kColor, 0xFFFFFF);
  return breathe;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_breathe_is_registered() {
  const EffectDescriptor* breathe = EffectRegistry::global().find("breathe");
  TEST_ASSERT_NOT_NULL(breathe);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(5, breathe->paramCount);
  TEST_ASSERT_EQUAL_INT(kMinLevel, ledframe::findParam(*breathe, "min_level"));
  TEST_ASSERT_EQUAL_INT(kPeriod, ledframe::findParam(*breathe, "period"));
}

// One breath per period: min level at the start, full in the middle, min at the end.
void test_breathe_follows_the_period() {
  Breathe breathe;
  white(breathe);
  TEST_ASSERT_EQUAL_UINT8(24, colorAt(breathe, 0).g);
  TEST_ASSERT_EQUAL_UINT8(255, colorAt(breathe, 2500).g);
  TEST_ASSERT_EQUAL_UINT8(24, colorAt(breathe, 5000).g);
  TEST_ASSERT_EQUAL_UINT8(255, colorAt(breathe, 7500).g);
}

// The level rises in the first half and falls in the second, without jumps.
void test_breathe_is_smooth() {
  Breathe breathe;
  white(breathe);
  int before = colorAt(breathe, 0).g;
  for (uint32_t ms = 20; ms <= 5000; ms += 20) {
    const int level = colorAt(breathe, ms).g;
    if (ms <= 2500) {
      TEST_ASSERT_TRUE(level >= before);
    } else {
      TEST_ASSERT_TRUE(level <= before);
    }
    TEST_ASSERT_TRUE(level - before <= 6 && before - level <= 6);
    before = level;
  }
}

// The sine is squared: a quarter period gives a quarter of the range, not half.
void test_breathe_curve_is_squared() {
  Breathe breathe;
  white(breathe);
  breathe.change(kMinLevel, 0);
  const int quarter = colorAt(breathe, 1250).g;
  TEST_ASSERT_TRUE(quarter >= 62 && quarter <= 66);
}

void test_breathe_uses_the_color() {
  Breathe breathe;
  breathe.change(kColor, 0xFF8000);
  TEST_ASSERT_TRUE(colorAt(breathe, 2500) == CRGB(0xFF, 0x80, 0x00));
  breathe.gamma = true;
  breathe.change(kColor, 0xFF8000);  // dirty: the next frame redraws
  FrameContext ctx = breathe.context(0);
  TEST_ASSERT_TRUE(colorAt(breathe, 2500) == ctx.userColor(0xFF8000));
}

// A frame with the same color as the last one is not sent again.
void test_breathe_skips_unchanged_frames() {
  Breathe breathe;
  breathe.change(kPeriod, 30000);
  TEST_ASSERT_TRUE(breathe.render(0));
  TEST_ASSERT_FALSE(breathe.render(20));  // still at the bottom of a slow breath
  TEST_ASSERT_TRUE(breathe.render(7500));
  breathe.change(kMinLevel, 255);  // steady light
  TEST_ASSERT_TRUE(breathe.render(7500));
  TEST_ASSERT_FALSE(breathe.render(9000));
  TEST_ASSERT_FALSE(breathe.render(20000));
}

// With colors = palette the color walks through the palette, once in 8 breaths.
void test_breathe_palette_walks() {
  Breathe breathe;
  breathe.change(kColors, 1);
  breathe.change(kPalette, Palettes::indexOf("rainbow"));
  const CRGB first = colorAt(breathe, 2500);
  const CRGB fifth = colorAt(breathe, 4 * 5000 + 2500);
  TEST_ASSERT_FALSE(first == fifth);
  const CRGB ninth = colorAt(breathe, 8 * 5000 + 2500);
  TEST_ASSERT_TRUE(first == ninth);
  const CRGB dark = colorAt(breathe, 4 * 5000);
  TEST_ASSERT_TRUE(dark.r + dark.g + dark.b < fifth.r + fifth.g + fifth.b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_breathe_is_registered);
  RUN_TEST(test_breathe_follows_the_period);
  RUN_TEST(test_breathe_is_smooth);
  RUN_TEST(test_breathe_curve_is_squared);
  RUN_TEST(test_breathe_uses_the_color);
  RUN_TEST(test_breathe_skips_unchanged_frames);
  RUN_TEST(test_breathe_palette_walks);
  return UNITY_END();
}

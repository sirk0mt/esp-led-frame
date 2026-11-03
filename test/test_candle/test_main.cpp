#include <unity.h>

#include <cstdlib>
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

enum P : uint8_t { kIntensity, kSpeed, kColor };

Canvas canvas;
Palettes palettes;

// One candle with its own parameter values: defaults, then the given changes.
struct Flame {
  Flame() {
    descriptor = EffectRegistry::global().find("candle");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx{canvas, 0, 0, params, palettes.get(0)};
    effect->begin(ctx);
  }

  std::vector<uint32_t> frameAt(uint32_t nowMs) {
    FrameContext ctx{canvas, nowMs, 20, params, palettes.get(0)};
    effect->render(ctx);
    std::vector<uint32_t> out;
    for (int i = 0; i < canvas.size(); ++i) {
      out.push_back((canvas[i].r << 16) | (canvas[i].g << 8) | canvas[i].b);
    }
    return out;
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

int red(uint32_t rgb) {
  return static_cast<int>(rgb >> 16);
}
int green(uint32_t rgb) {
  return static_cast<int>((rgb >> 8) & 0xFF);
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_candle_is_registered() {
  const EffectDescriptor* candle = EffectRegistry::global().find("candle");
  TEST_ASSERT_NOT_NULL(candle);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(3, candle->paramCount);
  TEST_ASSERT_EQUAL_INT32(0xFF7A1E, candle->params[kColor].def);
}

// Intensity 0: a steady light in exactly the chosen color.
void test_candle_intensity_0_is_steady() {
  Flame candle;
  candle.params[kIntensity] = 0;
  for (uint32_t ms = 0; ms < 5000; ms += 250) {
    for (uint32_t rgb : candle.frameAt(ms)) {
      TEST_ASSERT_EQUAL_HEX32(0xFF7A1E, rgb);
    }
  }
}

// Full intensity dims the flame down to a third of the seen light (PWM about 1/9)
// at times, and brings it back to almost full at others.
void test_candle_flickers() {
  Flame candle;
  candle.params[kIntensity] = 100;
  int low = 255;
  int high = 0;
  for (uint32_t ms = 0; ms < 20000; ms += 40) {
    for (uint32_t rgb : candle.frameAt(ms)) {
      low = red(rgb) < low ? red(rgb) : low;
      high = red(rgb) > high ? red(rgb) : high;
    }
  }
  TEST_ASSERT_TRUE(low < 60);
  TEST_ASSERT_TRUE(high > 200);
}

// A dimmer flame is redder: green falls faster than red.
void test_candle_dim_is_redder() {
  Flame candle;
  candle.params[kIntensity] = 100;
  candle.params[kColor] = 0xFFFFFF;  // white shows the shift best
  double dimRatio = 1;
  double brightRatio = 0;
  int dimRed = 256;
  int brightRed = -1;
  for (uint32_t ms = 0; ms < 20000; ms += 40) {
    for (uint32_t rgb : candle.frameAt(ms)) {
      const int r = red(rgb);
      if (r < dimRed && r > 20) {
        dimRed = r;
        dimRatio = static_cast<double>(green(rgb)) / r;
      }
      if (r > brightRed) {
        brightRed = r;
        brightRatio = static_cast<double>(green(rgb)) / r;
      }
    }
  }
  TEST_ASSERT_TRUE(dimRatio < brightRatio * 0.8);
}

// The flicker is gentle: no channel changes by more than 40 between 20 ms frames at
// the default speed, and neighbors flicker alike (the noise is spread in space).
void test_candle_is_smooth() {
  Flame candle;
  candle.params[kIntensity] = 100;
  std::vector<uint32_t> before = candle.frameAt(0);
  int maxStep = 0;
  for (uint32_t ms = 20; ms <= 10000; ms += 20) {
    const std::vector<uint32_t> now = candle.frameAt(ms);
    for (size_t i = 0; i < now.size(); ++i) {
      const int step = std::abs(red(now[i]) - red(before[i]));
      maxStep = step > maxStep ? step : maxStep;
    }
    before = now;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT(40, maxStep);

  const std::vector<uint32_t> frame = candle.frameAt(4321);
  long neighbor = 0;
  int pairs = 0;
  for (int i = 0; i + 1 < canvas.size(); ++i) {
    if (i % canvas.width() + 1 < canvas.width()) {
      neighbor += std::abs(red(frame[i]) - red(frame[i + 1]));
      ++pairs;
    }
  }
  TEST_ASSERT_TRUE(neighbor / pairs < 12);
}

void test_candle_depends_only_on_time() {
  Flame a;
  a.frameAt(100);
  Flame b;
  TEST_ASSERT_TRUE(a.frameAt(9876) == b.frameAt(9876));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_candle_is_registered);
  RUN_TEST(test_candle_intensity_0_is_steady);
  RUN_TEST(test_candle_flickers);
  RUN_TEST(test_candle_dim_is_redder);
  RUN_TEST(test_candle_is_smooth);
  RUN_TEST(test_candle_depends_only_on_time);
  return UNITY_END();
}

#include <unity.h>

#include <cstdlib>
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

enum P : uint8_t { kDensity, kSpeed, kPalette, kBackground };

Canvas canvas;
Palettes palettes;
// All white: on a black background the green channel of a pixel is its star level.
CRGBPalette16 white;

// One twinkle effect with its own parameter values on the white palette.
struct Twinkle {
  Twinkle() {
    descriptor = EffectRegistry::global().find("twinkle");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs) { return FrameContext{canvas, nowMs, 20, params, white}; }
  bool render(uint32_t nowMs) {
    FrameContext ctx = context(nowMs);
    return effect->render(ctx);
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

// Star level (green channel) of every pixel.
std::vector<int> frameAt(Twinkle& twinkle, uint32_t nowMs) {
  twinkle.render(nowMs);
  std::vector<int> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back(canvas[i].g);
  }
  return out;
}

// Mean share of lit pixels over 200 s.
double litShare(Twinkle& twinkle) {
  int lit = 0;
  int total = 0;
  for (uint32_t ms = 0; ms < 200000; ms += 1000) {
    for (int value : frameAt(twinkle, ms)) {
      lit += value > 0 ? 1 : 0;
      ++total;
    }
  }
  return static_cast<double>(lit) / total;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  for (uint8_t i = 0; i < 16; ++i) {
    white.entries[i] = CRGB(255, 255, 255);
  }
}
void tearDown() {}

void test_twinkle_is_registered() {
  const EffectDescriptor* twinkle = EffectRegistry::global().find("twinkle");
  TEST_ASSERT_NOT_NULL(twinkle);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, twinkle->paramCount);
  TEST_ASSERT_EQUAL_INT32(1, twinkle->params[kPalette].def);  // party
  TEST_ASSERT_EQUAL_INT(kBackground, ledframe::findParam(*twinkle, "background"));
}

// No state: the picture at a given time does not depend on the frames before it.
void test_twinkle_depends_only_on_time() {
  Twinkle fresh;
  const std::vector<int> expected = frameAt(fresh, 12345);
  Twinkle used;
  for (uint32_t ms = 0; ms < 30000; ms += 700) {
    used.render(ms);
  }
  TEST_ASSERT_TRUE(frameAt(used, 12345) == expected);
  TEST_ASSERT_TRUE(used.render(12345));  // a time-based effect always redraws
}

// Pixels are not in step: at one moment the lit pixels have many different levels.
void test_twinkle_pixels_are_independent() {
  Twinkle twinkle;
  std::set<int> levels;
  for (int value : frameAt(twinkle, 5000)) {
    levels.insert(value);
  }
  TEST_ASSERT_TRUE(levels.size() > 20);
}

// Density sets the share of lit pixels: density % of cycles light up, and a lit cycle
// is dark for about 7% of its length (both soft ends).
void test_twinkle_density_sets_the_lit_share() {
  Twinkle twinkle;
  twinkle.params[kDensity] = 40;
  const double share40 = litShare(twinkle);
  TEST_ASSERT_TRUE(share40 > 0.30 && share40 < 0.45);
  twinkle.params[kDensity] = 100;
  TEST_ASSERT_TRUE(litShare(twinkle) > 0.88);
  twinkle.params[kDensity] = 1;
  TEST_ASSERT_TRUE(litShare(twinkle) < 0.03);
}

// Stars rise and fade softly: at the default speed no pixel changes by more than 20
// levels in a 20 ms frame, yet every pixel reaches full brightness within a minute.
void test_twinkle_fades_softly() {
  Twinkle twinkle;
  twinkle.params[kDensity] = 100;
  std::vector<int> peak(canvas.size(), 0);
  std::vector<int> before = frameAt(twinkle, 0);
  int maxStep = 0;
  for (uint32_t ms = 20; ms < 60000; ms += 20) {
    const std::vector<int> now = frameAt(twinkle, ms);
    for (int i = 0; i < canvas.size(); ++i) {
      const int step = std::abs(now[i] - before[i]);
      maxStep = step > maxStep ? step : maxStep;
      peak[i] = now[i] > peak[i] ? now[i] : peak[i];
    }
    before = now;
  }
  TEST_ASSERT_TRUE(maxStep <= 20);
  for (int value : peak) {
    TEST_ASSERT_TRUE(value >= 250);
  }
}

// Speed scales time: speed 60 at t shows the same picture as speed 15 at 4t.
void test_twinkle_speed_scales_time() {
  Twinkle slow;
  slow.params[kSpeed] = 15;
  Twinkle fast;
  fast.params[kSpeed] = 60;
  TEST_ASSERT_TRUE(frameAt(fast, 7000) == frameAt(slow, 28000));
  TEST_ASSERT_FALSE(frameAt(fast, 7000) == frameAt(slow, 7000));
}

// Dark pixels show the background color, and stars blend over it.
void test_twinkle_background() {
  Twinkle twinkle;
  twinkle.params[kBackground] = 0x102030;
  twinkle.render(3000);
  int background = 0;
  int brighter = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    const CRGB& c = canvas[i];
    if (c == CRGB(0x10, 0x20, 0x30)) {
      ++background;
    } else {
      TEST_ASSERT_TRUE(c.r >= 0x10 && c.g >= 0x20 && c.b >= 0x30);  // white star over it
      ++brighter;
    }
  }
  TEST_ASSERT_TRUE(background > 50);
  TEST_ASSERT_TRUE(brighter > 10);
}

// The phase needs 64 bits: late times at full speed still fade softly.
void test_twinkle_late_times() {
  Twinkle twinkle;
  twinkle.params[kSpeed] = 100;
  twinkle.params[kDensity] = 100;
  const uint32_t late = 4000000000u;
  std::vector<int> before = frameAt(twinkle, late);
  int maxStep = 0;
  int lit = 0;
  for (uint32_t ms = late + 10; ms < late + 3000; ms += 10) {
    const std::vector<int> now = frameAt(twinkle, ms);
    for (int i = 0; i < canvas.size(); ++i) {
      const int step = std::abs(now[i] - before[i]);
      maxStep = step > maxStep ? step : maxStep;
      lit += now[i] > 0 ? 1 : 0;
    }
    before = now;
  }
  TEST_ASSERT_TRUE(maxStep <= 70);
  TEST_ASSERT_TRUE(lit > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_twinkle_is_registered);
  RUN_TEST(test_twinkle_depends_only_on_time);
  RUN_TEST(test_twinkle_pixels_are_independent);
  RUN_TEST(test_twinkle_density_sets_the_lit_share);
  RUN_TEST(test_twinkle_fades_softly);
  RUN_TEST(test_twinkle_speed_scales_time);
  RUN_TEST(test_twinkle_background);
  RUN_TEST(test_twinkle_late_times);
  return UNITY_END();
}

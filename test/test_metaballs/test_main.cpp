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

enum P : uint8_t { kSpeed, kCount, kPalette };

Canvas canvas;
// Black to white: the brightness of a pixel is its field index.
CRGBPalette16 gray;

// One metaballs effect with its own parameter values on the gray palette.
struct Metaballs {
  Metaballs() {
    descriptor = EffectRegistry::global().find("metaballs");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx{canvas, 0, 0, params, gray};
    effect->begin(ctx);
  }

  std::vector<int> frameAt(uint32_t nowMs) {
    FrameContext ctx{canvas, nowMs, 20, params, gray};
    effect->render(ctx);
    std::vector<int> out;
    for (int i = 0; i < canvas.size(); ++i) {
      out.push_back(canvas[i].g);
    }
    return out;
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

long total(const std::vector<int>& frame) {
  long sum = 0;
  for (int value : frame) {
    sum += value;
  }
  return sum;
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

void test_metaballs_is_registered() {
  const EffectDescriptor* metaballs = EffectRegistry::global().find("metaballs");
  TEST_ASSERT_NOT_NULL(metaballs);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(3, metaballs->paramCount);
  TEST_ASSERT_EQUAL_INT32(4, metaballs->params[kCount].def);
}

void test_metaballs_depend_only_on_time() {
  Metaballs a;
  a.frameAt(1000);
  Metaballs b;
  TEST_ASSERT_TRUE(a.frameAt(7777) == b.frameAt(7777));
}

// One ball: the field is brightest at the ball and fades with the square of the
// distance, so pixels 7 or more pixels from the brightest one are under a quarter
// of it (R = 2 pixels: field 4 / 49 there, below the background level, so dark).
void test_metaballs_single_ball_fades_out() {
  Metaballs metaballs;
  metaballs.params[kCount] = 1;
  const std::vector<int> frame = metaballs.frameAt(3000);
  const int width = canvas.width();
  int best = 0;
  for (int i = 1; i < canvas.size(); ++i) {
    best = frame[i] > frame[best] ? i : best;
  }
  const int bx = best % width;
  const int by = best / width;
  TEST_ASSERT_TRUE(frame[best] > 200);
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < width; ++x) {
      const int dx = x - bx;
      const int dy = y - by;
      if (dx * dx + dy * dy >= 49) {
        TEST_ASSERT_TRUE(frame[y * width + x] < frame[best] / 4);
      }
    }
  }
}

// More balls add more field: the frame gets brighter, and the blobs cover more.
void test_metaballs_count_adds_light() {
  Metaballs one;
  one.params[kCount] = 1;
  Metaballs six;
  six.params[kCount] = 6;
  long light1 = 0;
  long light6 = 0;
  for (uint32_t ms = 0; ms < 10000; ms += 500) {
    light1 += total(one.frameAt(ms));
    light6 += total(six.frameAt(ms));
  }
  TEST_ASSERT_TRUE(light6 > light1 * 3);
}

// The balls drift: frames 1 s apart differ, but between 20 ms frames no pixel jumps.
// At the default speed a ball moves at most about 0.22 pixel per frame (swing 6.3
// pixels, 0.275 turns per second), and at the edge of a ball the index changes by
// about 250 per pixel, so a pixel changes by up to about 60 per frame, never by a
// whole jump of 255.
void test_metaballs_move_smoothly() {
  Metaballs metaballs;
  TEST_ASSERT_FALSE(metaballs.frameAt(2000) == metaballs.frameAt(3000));
  std::vector<int> before = metaballs.frameAt(0);
  int maxStep = 0;
  for (uint32_t ms = 20; ms <= 10000; ms += 20) {
    const std::vector<int> now = metaballs.frameAt(ms);
    for (size_t i = 0; i < now.size(); ++i) {
      maxStep = std::abs(now[i] - before[i]) > maxStep ? std::abs(now[i] - before[i]) : maxStep;
    }
    before = now;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT(80, maxStep);
}

// The sine angles come from 64-bit products, so late times do not jump.
void test_metaballs_late_times() {
  Metaballs metaballs;
  metaballs.params[kSpeed] = 100;
  const uint32_t late = 4000000000u;
  std::vector<int> before = metaballs.frameAt(late);
  int maxStep = 0;
  for (uint32_t ms = late + 20; ms < late + 2000; ms += 20) {
    const std::vector<int> now = metaballs.frameAt(ms);
    for (size_t i = 0; i < now.size(); ++i) {
      maxStep = std::abs(now[i] - before[i]) > maxStep ? std::abs(now[i] - before[i]) : maxStep;
    }
    before = now;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT(160, maxStep);
}

void test_metaballs_on_extreme_layouts() {
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Metaballs dot;
  dot.frameAt(1000);
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  Metaballs row;
  row.frameAt(1000);
  TEST_ASSERT_TRUE(canvas.resize(1, ledframe::kMaxLeds));
  Metaballs column;
  column.frameAt(1000);
  TEST_ASSERT_TRUE(canvas.resize(32, 32));
  Metaballs square;
  square.params[kCount] = 6;
  TEST_ASSERT_TRUE(total(square.frameAt(1000)) > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_metaballs_is_registered);
  RUN_TEST(test_metaballs_depend_only_on_time);
  RUN_TEST(test_metaballs_single_ball_fades_out);
  RUN_TEST(test_metaballs_count_adds_light);
  RUN_TEST(test_metaballs_move_smoothly);
  RUN_TEST(test_metaballs_late_times);
  RUN_TEST(test_metaballs_on_extreme_layouts);
  return UNITY_END();
}

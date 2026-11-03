#include <lib8tion.h>
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

enum P : uint8_t { kSpeed, kDensity, kColors, kColor };

Canvas canvas;
Palettes palettes;

// One rain effect with its own parameter values: defaults, then the given changes.
struct Rain {
  Rain() {
    random16_set_seed(1234);
    descriptor = EffectRegistry::global().find("rain");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    canvas.clear();  // like the engine: begin() sees a clear canvas
    FrameContext ctx = context(0, 0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs, uint32_t deltaMs) {
    return FrameContext{canvas, nowMs, deltaMs, params, palettes.get(0)};
  }
  bool render(uint32_t nowMs, uint32_t deltaMs) {
    FrameContext ctx = context(nowMs, deltaMs);
    return effect->render(ctx);
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

// Runs the rain to totalMs in frames of frameMs and returns the last frame.
std::vector<uint32_t> runTo(Rain& rain, uint32_t totalMs, uint32_t frameMs) {
  rain.render(0, 0);
  for (uint32_t ms = frameMs; ms <= totalMs; ms += frameMs) {
    rain.render(ms, frameMs);
  }
  return pixels();
}

// Brightness of a pixel as the sum of its channels.
int light(int x, int y) {
  const CRGB& c = canvas.at(x, y);
  return c.r + c.g + c.b;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_rain_is_registered() {
  const EffectDescriptor* rain = EffectRegistry::global().find("rain");
  TEST_ASSERT_NOT_NULL(rain);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, rain->paramCount);
  TEST_ASSERT_EQUAL_INT(kColor, ledframe::findParam(*rain, "color"));
  TEST_ASSERT_EQUAL_INT32(0x00FF40, rain->params[kColor].def);
}

// At 10 rows per second a drop moves one row every 100 ms. A new drop starts just
// above the matrix and glides in: half way through a step the top row looks half lit
// (PWM level 1/4, see test_rain_ramps_for_the_eye), and the white of the head fades
// out as it moves on.
void test_rain_glides_between_steps() {
  Rain rain;
  rain.params[kSpeed] = 10;
  rain.params[kDensity] = 50;
  rain.params[kColor] = 0x00FF00;
  rain.render(0, 0);
  rain.render(100, 100);  // the first drops start above the matrix: nothing to see yet
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_EQUAL_INT(0, canvas[i].r + canvas[i].g + canvas[i].b);
  }
  TEST_ASSERT_TRUE(rain.render(150, 50));  // every frame shows motion, not only steps
  int column = -1;
  for (int x = 0; x < canvas.width() && column < 0; ++x) {
    column = light(x, 0) > 0 ? x : -1;
  }
  TEST_ASSERT_TRUE(column >= 0);
  TEST_ASSERT_TRUE(canvas.at(column, 0).g > 55 && canvas.at(column, 0).g < 75);  // half lit
  TEST_ASSERT_EQUAL_INT(0, light(column, 1));

  rain.render(200, 50);  // the head is in the top row: full, half way to white
  TEST_ASSERT_TRUE(canvas.at(column, 0) == CRGB(127, 255, 127));
  rain.render(250, 50);  // moving on: less white above, the next row half lit
  TEST_ASSERT_TRUE(canvas.at(column, 0).r > 40 && canvas.at(column, 0).r < 90);
  TEST_ASSERT_TRUE(canvas.at(column, 1).g > 55 && canvas.at(column, 1).g < 75);
  rain.render(300, 50);
  TEST_ASSERT_TRUE(canvas.at(column, 0) == CRGB(0, 255, 0));  // the pure drop color
  TEST_ASSERT_TRUE(canvas.at(column, 1) == CRGB(127, 255, 127));
}

// Every drop falls one row per step and leaves a trail in the drop color that fades.
void test_rain_drops_fall_with_a_trail() {
  Rain rain;
  rain.params[kSpeed] = 10;
  rain.params[kDensity] = 50;
  rain.params[kColor] = 0x00FF00;
  rain.render(0, 0);
  rain.render(100, 100);
  // Find a column whose drop, started in the first step, has its head in the top row.
  rain.render(200, 100);
  int column = -1;
  for (int x = 0; x < canvas.width() && column < 0; ++x) {
    column = light(x, 0) > 0 ? x : -1;
  }
  TEST_ASSERT_TRUE(column >= 0);
  const CRGB head = canvas.at(column, 0);
  TEST_ASSERT_TRUE(head.r > 0 && head.g == 255);  // lighter than the color: towards white
  for (int y = 1; y < canvas.height(); ++y) {
    TEST_ASSERT_EQUAL_INT(0, light(column, y));
  }

  rain.render(300, 100);
  TEST_ASSERT_TRUE(canvas.at(column, 1) == head);  // the head moved one row down
  const CRGB trail = canvas.at(column, 0);
  TEST_ASSERT_TRUE(trail.r == 0 && trail.b == 0 && trail.g == 255);  // the pure color

  rain.render(400, 100);
  rain.render(500, 100);
  TEST_ASSERT_TRUE(canvas.at(column, 3) == head);
  // The trail fades: the older the pixel, the darker.
  TEST_ASSERT_TRUE(canvas.at(column, 2).g > canvas.at(column, 1).g);
  TEST_ASSERT_TRUE(canvas.at(column, 1).g > canvas.at(column, 0).g);
  TEST_ASSERT_TRUE(canvas.at(column, 0).g > 0);
}

// The arriving head lights up evenly for the eye, which sees about the square root of
// the PWM level: at 1/4, 1/2 and 3/4 of a step the top row has PWM level 1/16, 1/4
// and 9/16 of full (16, 64 and 143 of 255, give or take one for the dithering).
void test_rain_ramps_for_the_eye() {
  Rain rain;
  rain.params[kSpeed] = 1;
  rain.params[kDensity] = 50;
  rain.params[kColor] = 0x00FF00;
  rain.render(0, 0);
  rain.render(1000, 1000);  // the first drops start above the matrix
  rain.render(1250, 250);
  int column = -1;
  for (int x = 0; x < canvas.width() && column < 0; ++x) {
    column = light(x, 0) > 0 ? x : -1;
  }
  TEST_ASSERT_TRUE(column >= 0);
  TEST_ASSERT_INT_WITHIN(2, 16, canvas.at(column, 0).g);
  rain.render(1500, 250);
  TEST_ASSERT_INT_WITHIN(2, 64, canvas.at(column, 0).g);
  rain.render(1750, 250);
  TEST_ASSERT_INT_WITHIN(2, 143, canvas.at(column, 0).g);
}

// Slow rain changes smoothly. A PWM ramp that looks even to the eye is steepest at
// its top: at 3 rows per second (16.7 frames of 20 ms per row) a channel changes by
// at most about 2 * 255 / 16.7 = 31 per frame, plus 1 for the dithering. At 1 row per
// second the bound is a third of that. A whole-row jump would change it by 255.
void test_rain_is_smooth_at_low_speed() {
  for (int32_t speed : {3, 1}) {
    Rain rain;
    rain.params[kSpeed] = speed;
    rain.params[kDensity] = 50;
    rain.render(0, 0);
    std::vector<uint32_t> before = pixels();
    int maxStep = 0;
    long total = 0;
    for (uint32_t ms = 20; ms <= 8000; ms += 20) {
      rain.render(ms, 20);
      const std::vector<uint32_t> now = pixels();
      for (size_t i = 0; i < now.size(); ++i) {
        for (int shift = 0; shift <= 16; shift += 8) {
          const int a = static_cast<int>((before[i] >> shift) & 0xFF);
          const int b = static_cast<int>((now[i] >> shift) & 0xFF);
          maxStep = std::abs(a - b) > maxStep ? std::abs(a - b) : maxStep;
          total += b;
        }
      }
      before = now;
    }
    TEST_ASSERT_TRUE(total > 0);
    TEST_ASSERT_TRUE(maxStep <= (speed == 3 ? 34 : 13));
  }
}

// A drop leaves at the bottom; its column can then start a new one.
void test_rain_drops_leave_and_return() {
  Rain rain;
  rain.params[kSpeed] = 10;
  rain.params[kDensity] = 50;
  rain.render(0, 0);
  int starts = 0;
  for (uint32_t ms = 100; ms <= 5000; ms += 100) {
    rain.render(ms, 100);
    for (int x = 0; x < canvas.width(); ++x) {
      // A head in the top row is a new drop: only heads are lighter than the color.
      starts += canvas.at(x, 0).r > 0 ? 1 : 0;
    }
  }
  // 15 columns, 50 steps, each drop takes 10 steps plus a wait: about 4 drops each.
  TEST_ASSERT_TRUE(starts > 40);
  TEST_ASSERT_TRUE(starts < 15 * 5 + 1);
}

// Density sets how many columns rain.
void test_rain_density() {
  Rain sparse;
  sparse.params[kDensity] = 2;
  Rain dense;
  dense.params[kDensity] = 40;
  long sparseLight = 0;
  long denseLight = 0;
  for (uint32_t ms = 0; ms <= 5000; ms += 20) {
    sparse.render(ms, ms == 0 ? 0 : 20);
    for (int i = 0; i < canvas.size(); ++i) {
      sparseLight += canvas[i].g;
    }
  }
  for (uint32_t ms = 0; ms <= 5000; ms += 20) {
    dense.render(ms, ms == 0 ? 0 : 20);
    for (int i = 0; i < canvas.size(); ++i) {
      denseLight += canvas[i].g;
    }
  }
  TEST_ASSERT_TRUE(sparseLight > 0);
  TEST_ASSERT_TRUE(denseLight > sparseLight * 3);
}

// All drawing happens in steps, so the same time gives the same picture at any FPS.
void test_rain_does_not_depend_on_fps() {
  Rain slow;
  const std::vector<uint32_t> at10ms = runTo(slow, 3000, 10);
  Rain fast;
  const std::vector<uint32_t> at50ms = runTo(fast, 3000, 50);
  TEST_ASSERT_TRUE(at10ms == at50ms);
}

// The test's copy of the head color: halfway from the drop color to white.
CRGB lighter(const CRGB& c) {
  return CRGB(qadd8(c.r, (255 - c.r) / 2), qadd8(c.g, (255 - c.g) / 2),
              qadd8(c.b, (255 - c.b) / 2));
}

// With colors = random every drop has its own hue, the same in its head and trail.
void test_rain_random_colors() {
  Rain rain;
  rain.params[kSpeed] = 10;
  rain.params[kDensity] = 50;
  rain.params[kColors] = 1;        // random
  rain.params[kColor] = 0x00FF00;  // ignored in this mode
  rain.render(0, 0);
  std::vector<uint32_t> hues;
  int heads = 0;
  for (uint32_t ms = 100; ms <= 6000; ms += 100) {
    rain.render(ms, 100);
    for (int x = 0; x < canvas.width(); ++x) {
      for (int y = 1; y < canvas.height(); ++y) {
        const CRGB trail = canvas.at(x, y - 1);
        const CRGB head = canvas.at(x, y);
        // A head under the trail pixel it left this step: trail full, head lighter.
        if (trail.r + trail.g + trail.b > 0 && head == lighter(trail) && !(head == trail)) {
          ++heads;
          const uint32_t rgb = (trail.r << 16) | (trail.g << 8) | trail.b;
          bool known = false;
          for (uint32_t h : hues) {
            known = known || h == rgb;
          }
          if (!known) {
            hues.push_back(rgb);
          }
        }
      }
    }
  }
  TEST_ASSERT_TRUE(heads > 100);
  TEST_ASSERT_TRUE(hues.size() > 15);  // many different drop colors, not just green
}

// Random colors are as independent of the FPS as one color.
void test_rain_random_colors_do_not_depend_on_fps() {
  Rain slow;
  slow.params[kColors] = 1;
  const std::vector<uint32_t> at10ms = runTo(slow, 3000, 10);
  Rain fast;
  fast.params[kColors] = 1;
  const std::vector<uint32_t> at50ms = runTo(fast, 3000, 50);
  TEST_ASSERT_TRUE(at10ms == at50ms);
}

void test_rain_on_extreme_layouts() {
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Rain dot;
  runTo(dot, 2000, 20);
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  Rain row;
  row.params[kSpeed] = 60;
  row.params[kDensity] = 50;
  runTo(row, 2000, 20);
  TEST_ASSERT_TRUE(canvas.resize(1, ledframe::kMaxLeds));
  Rain column;
  column.params[kSpeed] = 60;
  runTo(column, 2000, 20);
  TEST_ASSERT_TRUE(light(0, 0) + light(0, 50) + light(0, 100) >= 0);  // no crash
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rain_is_registered);
  RUN_TEST(test_rain_glides_between_steps);
  RUN_TEST(test_rain_drops_fall_with_a_trail);
  RUN_TEST(test_rain_ramps_for_the_eye);
  RUN_TEST(test_rain_is_smooth_at_low_speed);
  RUN_TEST(test_rain_drops_leave_and_return);
  RUN_TEST(test_rain_density);
  RUN_TEST(test_rain_does_not_depend_on_fps);
  RUN_TEST(test_rain_random_colors);
  RUN_TEST(test_rain_random_colors_do_not_depend_on_fps);
  RUN_TEST(test_rain_on_extreme_layouts);
  return UNITY_END();
}

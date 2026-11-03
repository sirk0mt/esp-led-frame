#include <lib8tion.h>
#include <unity.h>

#include <cmath>
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

// One starfield with its own parameter values: defaults with white stars, then the
// given changes.
struct Starfield {
  Starfield() {
    random16_set_seed(1234);
    descriptor = EffectRegistry::global().find("starfield");
    ledframe::loadDefaults(*descriptor, params);
    params[kColor] = 0xFFFFFF;
    effect = slot.create(*descriptor);
    canvas.clear();  // like the engine: begin() sees a clear canvas
    FrameContext ctx{canvas, 0, 0, params, palettes.get(0)};
    effect->begin(ctx);
  }

  bool render(uint32_t nowMs, uint32_t deltaMs) {
    FrameContext ctx{canvas, nowMs, deltaMs, params, palettes.get(0)};
    return effect->render(ctx);
  }

  // Renders frames of frameMs from the last time up to nowMs.
  void runTo(uint32_t nowMs, uint32_t frameMs = 10) {
    while (timeMs + frameMs <= nowMs) {
      timeMs += frameMs;
      render(timeMs, frameMs);
    }
  }

  // Renders 10 ms frames until a pixel lights up; returns false if none does by limitMs.
  bool runToFirstStar(uint32_t limitMs) {
    while (timeMs < limitMs) {
      runTo(timeMs + 10);
      if (litPixels() > 0) {
        return true;
      }
    }
    return false;
  }

  int litPixels() const {
    int lit = 0;
    for (int i = 0; i < canvas.size(); ++i) {
      lit += canvas[i].g > 0 ? 1 : 0;
    }
    return lit;
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
  uint32_t timeMs = 0;
};

std::vector<int> greens() {
  std::vector<int> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back(canvas[i].g);
  }
  return out;
}

// Center of the 15 x 10 matrix in pixels.
constexpr double kCenterX = 7.0;
constexpr double kCenterY = 4.5;

double distanceFromCenter(int x, int y) {
  return std::sqrt((x - kCenterX) * (x - kCenterX) + (y - kCenterY) * (y - kCenterY));
}

// The brightest pixel (green channel) as x, y and level.
void brightest(int& bx, int& by, int& level) {
  level = -1;
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      if (canvas.at(x, y).g > level) {
        level = canvas.at(x, y).g;
        bx = x;
        by = y;
      }
    }
  }
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_starfield_is_registered() {
  const EffectDescriptor* starfield = EffectRegistry::global().find("starfield");
  TEST_ASSERT_NOT_NULL(starfield);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, starfield->paramCount);
  TEST_ASSERT_EQUAL_INT(kDensity, ledframe::findParam(*starfield, "density"));
}

void test_starfield_is_deterministic() {
  Starfield a;
  a.runTo(3000);
  const std::vector<int> first = greens();
  Starfield b;
  b.runTo(3000);
  TEST_ASSERT_TRUE(first == greens());
}

// Light as the eye sees it, about the square root of the PWM level, 0..255.
double seen(int pwm) {
  return std::sqrt(pwm / 255.0) * 255.0;
}

// Stars at speed 1 stay within about half a pixel of the center (7, 4.5), between
// two rows, so their light is split over the pixels around them: at least 2 pixels,
// all in the block around the center, none holding a whole star. The shares are
// shares of the seen light, so together they look like one star on one pixel: after
// 2.1 s the first star is fully lit, seen light 128 plus a little for its distance.
void test_starfield_antialiasing() {
  Starfield starfield;
  starfield.params[kSpeed] = 1;  // the stars barely move
  starfield.params[kDensity] = 1;
  TEST_ASSERT_TRUE(starfield.runToFirstStar(10000));
  starfield.runTo(starfield.timeMs + 2100);
  int minX = 99, maxX = -1, minY = 99, maxY = -1, lit = 0;
  double sum = 0, top = 0;
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      const int g = canvas.at(x, y).g;
      if (g == 0) {
        continue;
      }
      ++lit;
      sum += seen(g);
      top = seen(g) > top ? seen(g) : top;
      minX = x < minX ? x : minX;
      maxX = x > maxX ? x : maxX;
      minY = y < minY ? y : minY;
      maxY = y > maxY ? y : maxY;
    }
  }
  TEST_ASSERT_TRUE(lit >= 2);
  TEST_ASSERT_TRUE(minX >= 6 && maxX <= 8 && minY >= 3 && maxY <= 6);
  TEST_ASSERT_TRUE(top < sum * 0.9);  // split, not one full pixel
  TEST_ASSERT_TRUE(sum >= 110);       // at least one fully lit star
}

// A new star lights up gradually: at speed 10 it takes 2 s while the star barely
// moves, so the first visible frame is dim and the light grows in small steps.
void test_starfield_stars_fade_in() {
  Starfield starfield;
  starfield.params[kSpeed] = 10;
  starfield.params[kDensity] = 1;
  TEST_ASSERT_TRUE(starfield.runToFirstStar(10000));
  int x = 0, y = 0, level = 0;
  brightest(x, y, level);
  TEST_ASSERT_TRUE(level < 10);
  int maxRise = 0;
  for (int frame = 0; frame < 60; ++frame) {  // 1.2 s: later stars would move fast
    starfield.runTo(starfield.timeMs + 20);
    int nx = 0, ny = 0, now = 0;
    brightest(nx, ny, now);
    maxRise = now - level > maxRise ? now - level : maxRise;
    level = now;
  }
  TEST_ASSERT_GREATER_THAN_INT(15, level);    // it did light up
  TEST_ASSERT_LESS_OR_EQUAL_INT(3, maxRise);  // without a jump
}

// A star flies out and gets brighter: speed 50 takes it more than 2 pixels out in
// 0.8 s. Later stars are dimmer near the center, so the brightest pixel follows it.
void test_starfield_stars_fly_out() {
  Starfield starfield;
  starfield.params[kSpeed] = 50;
  starfield.params[kDensity] = 1;
  TEST_ASSERT_TRUE(starfield.runToFirstStar(10000));
  int x0 = 0, y0 = 0, level0 = 0;
  brightest(x0, y0, level0);
  starfield.runTo(starfield.timeMs + 800);
  int x1 = 0, y1 = 0, level1 = 0;
  brightest(x1, y1, level1);
  TEST_ASSERT_TRUE(distanceFromCenter(x1, y1) > distanceFromCenter(x0, y0) + 2.0);
  TEST_ASSERT_TRUE(level1 > level0 * 2);
}

// Directions are random: within 10 s light reaches every quadrant.
void test_starfield_all_directions() {
  Starfield starfield;
  bool quadrant[4] = {false, false, false, false};
  for (uint32_t ms = 500; ms <= 10000; ms += 500) {
    starfield.runTo(ms);
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        if (canvas.at(x, y).g > 0 && x != 7) {
          quadrant[(x > 7 ? 1 : 0) + (y > 4 ? 2 : 0)] = true;
        }
      }
    }
  }
  for (bool seen : quadrant) {
    TEST_ASSERT_TRUE(seen);
  }
}

// Slow stars stay near the center; the full star slots (64) are no problem.
void test_starfield_slow_and_full() {
  Starfield starfield;
  starfield.params[kSpeed] = 1;
  starfield.params[kDensity] = 100;  // one star per step: the slots fill in 0.64 s
  starfield.runTo(3000);
  TEST_ASSERT_TRUE(starfield.litPixels() > 0);
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      if (canvas.at(x, y).g > 0) {
        TEST_ASSERT_TRUE(distanceFromCenter(x, y) < 2.0);
      }
    }
  }
}

// The stars move in fixed 10 ms steps, so frames of 10 and 40 ms show the same stars;
// only the trails fade in slightly different amounts.
void test_starfield_does_not_depend_on_fps() {
  Starfield a;
  a.runTo(4000, 10);
  const std::vector<int> at10ms = greens();
  Starfield b;
  b.runTo(4000, 40);
  const std::vector<int> at40ms = greens();
  double sum = 0;
  for (size_t i = 0; i < at10ms.size(); ++i) {
    sum += std::abs(at10ms[i] - at40ms[i]);
  }
  TEST_ASSERT_TRUE(sum / at10ms.size() < 6.0);
  int x10 = 0, y10 = 0, level10 = 0;
  canvas.clear();
  for (size_t i = 0; i < at10ms.size(); ++i) {
    canvas[i].g = static_cast<uint8_t>(at10ms[i]);
  }
  brightest(x10, y10, level10);
  TEST_ASSERT_TRUE(level10 > 0);
  TEST_ASSERT_TRUE(at40ms[y10 * canvas.width() + x10] > 0);  // the same star is there
}

void test_starfield_uses_the_color() {
  Starfield starfield;
  starfield.params[kColor] = 0xFF0000;
  bool red = false;
  for (uint32_t ms = 500; ms <= 3000; ms += 500) {
    starfield.runTo(ms);
    for (int i = 0; i < canvas.size(); ++i) {
      TEST_ASSERT_EQUAL_UINT8(0, canvas[i].g);
      TEST_ASSERT_EQUAL_UINT8(0, canvas[i].b);
      red = red || canvas[i].r > 0;
    }
  }
  TEST_ASSERT_TRUE(red);
}

// With colors = random the stars have many different hues; the color is ignored.
void test_starfield_random_colors() {
  Starfield starfield;
  starfield.params[kColors] = 1;  // random
  starfield.params[kColor] = 0xFF0000;
  bool redMost = false, greenMost = false, blueMost = false;
  for (uint32_t ms = 500; ms <= 6000; ms += 500) {
    starfield.runTo(ms);
    for (int i = 0; i < canvas.size(); ++i) {
      const CRGB& c = canvas[i];
      if (c.r + c.g + c.b < 60) {
        continue;  // too dim to tell the hue
      }
      redMost = redMost || (c.r > c.g && c.r > c.b);
      greenMost = greenMost || (c.g > c.r && c.g > c.b);
      blueMost = blueMost || (c.b > c.r && c.b > c.g);
    }
  }
  TEST_ASSERT_TRUE(redMost && greenMost && blueMost);
}

void test_starfield_on_extreme_layouts() {
  TEST_ASSERT_TRUE(canvas.resize(1, 1));
  Starfield dot;
  dot.runTo(2000);
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  Starfield row;
  row.params[kSpeed] = 100;
  row.runTo(5000);
  TEST_ASSERT_TRUE(row.litPixels() > 0);
  TEST_ASSERT_TRUE(canvas.resize(1, ledframe::kMaxLeds));
  Starfield column;
  column.runTo(2000);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starfield_is_registered);
  RUN_TEST(test_starfield_is_deterministic);
  RUN_TEST(test_starfield_antialiasing);
  RUN_TEST(test_starfield_stars_fade_in);
  RUN_TEST(test_starfield_stars_fly_out);
  RUN_TEST(test_starfield_all_directions);
  RUN_TEST(test_starfield_slow_and_full);
  RUN_TEST(test_starfield_does_not_depend_on_fps);
  RUN_TEST(test_starfield_uses_the_color);
  RUN_TEST(test_starfield_random_colors);
  RUN_TEST(test_starfield_on_extreme_layouts);
  return UNITY_END();
}

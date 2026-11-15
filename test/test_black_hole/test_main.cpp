#include <unity.h>

#include <cstdio>
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

enum P : uint8_t {
  kFade,
  kOuterX,
  kOuterY,
  kInnerX,
  kInnerY,
  kSize,
  kColors,
  kColor,
  kBlur,
  kPalette
};

Canvas canvas;
Palettes palettes;

// One black hole, rendered in frames of the given length.
struct BlackHole {
  BlackHole() {
    descriptor = EffectRegistry::global().find("black-hole");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t deltaMs) {
    return FrameContext{canvas, nowMs, deltaMs, params, palettes.get(params[kPalette])};
  }
  bool frame(uint32_t deltaMs) {
    nowMs += deltaMs;
    FrameContext ctx = context(deltaMs);
    return effect->render(ctx);
  }
  void run(uint32_t totalMs, uint32_t frameMs) {
    for (uint32_t elapsed = 0; elapsed < totalMs; elapsed += frameMs) {
      frame(frameMs);
    }
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
  uint32_t nowMs = 0;
};

std::vector<uint32_t> pixels() {
  std::vector<uint32_t> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back((canvas[i].r << 16) | (canvas[i].g << 8) | canvas[i].b);
  }
  return out;
}

// Largest channel of a pixel from pixels().
int brightness(uint32_t rgb) {
  const int r = (rgb >> 16) & 0xFF;
  const int g = (rgb >> 8) & 0xFF;
  const int b = rgb & 0xFF;
  return r > g ? (r > b ? r : b) : (g > b ? g : b);
}

int totalLight() {
  int sum = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    sum += canvas[i].r + canvas[i].g + canvas[i].b;
  }
  return sum;
}

int litPixels() {
  int count = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    count += (canvas[i].r | canvas[i].g | canvas[i].b) != 0 ? 1 : 0;
  }
  return count;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_black_hole_is_registered() {
  const EffectDescriptor* hole = EffectRegistry::global().find("black-hole");
  TEST_ASSERT_NOT_NULL(hole);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(10, hole->paramCount);
  TEST_ASSERT_TRUE((hole->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((hole->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kOuterX, ledframe::findParam(*hole, "outer_x"));
  TEST_ASSERT_EQUAL_INT(kInnerY, ledframe::findParam(*hole, "inner_y"));
  TEST_ASSERT_EQUAL_INT(kSize, ledframe::findParam(*hole, "size"));
  TEST_ASSERT_EQUAL_INT(kColor, ledframe::findParam(*hole, "color"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*hole, "palette"));
}

void test_frame_without_a_step_is_not_drawn() {
  BlackHole hole;
  TEST_ASSERT_FALSE(hole.frame(0));   // no time passed
  TEST_ASSERT_FALSE(hole.frame(20));  // less than one step (24 ms)
  TEST_ASSERT_TRUE(hole.frame(10));   // 30 ms in total: one step
  TEST_ASSERT_TRUE(litPixels() > 0);
}

void test_same_time_gives_the_same_picture() {
  BlackHole first;
  first.run(2000, 20);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  BlackHole second;
  second.run(2000, 20);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_picture_does_not_depend_on_fps() {
  // Steps run at a fixed rate and the stars follow the step clock, so the same time
  // gives the same steps at any frame rate.
  BlackHole fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  BlackHole slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_center_is_white() {
  // 15 x 10: the middle (7, 4.5) lies between two pixels, which are both white.
  BlackHole hole;
  hole.run(3000, 20);
  TEST_ASSERT_TRUE(canvas.at(7, 4) == CRGB(255, 255, 255));
  TEST_ASSERT_TRUE(canvas.at(7, 5) == CRGB(255, 255, 255));
  // 9 x 9: one pixel in the middle.
  TEST_ASSERT_TRUE(canvas.resize(9, 9));
  BlackHole square;
  square.frame(24);
  TEST_ASSERT_TRUE(canvas.at(4, 4) == CRGB(255, 255, 255));
}

void test_stars_move_and_leave_trails() {
  BlackHole hole;
  hole.run(3000, 20);
  const std::vector<uint32_t> before = pixels();
  const int lit = litPixels();
  hole.run(1000, 20);
  TEST_ASSERT_FALSE(before == pixels());
  // 13 soft dots of radius 1.2 light about 3 pixels each; the trails light many more.
  TEST_ASSERT_TRUE(lit > 13 * 3);
}

void test_stronger_fade_gives_shorter_trails() {
  BlackHole slow;
  slow.params[kFade] = 0;
  slow.run(3000, 20);
  const int longTrails = totalLight();
  canvas.clear();
  BlackHole fast;
  fast.params[kFade] = 31;
  fast.run(3000, 20);
  TEST_ASSERT_TRUE(totalLight() < longTrails);
}

void test_larger_stars_light_more_pixels() {
  BlackHole small;
  small.params[kSize] = 10;
  small.frame(24);  // one step on a black matrix: only the stars and the center
  const int smallLit = litPixels();
  canvas.clear();
  BlackHole large;
  large.params[kSize] = 25;
  large.frame(24);
  TEST_ASSERT_TRUE(smallLit > 0);
  TEST_ASSERT_TRUE(litPixels() > smallLit);
}

void test_slow_stars_glide_without_jumps() {
  // The stars move in fractions of a pixel and are drawn as soft dots, so a pixel that a
  // star reaches brightens over several steps. Moved from pixel to pixel, a star lights
  // a new pixel from dark to full in one step. Pixels only darken by the trail fade, so
  // the largest rise of a pixel per step shows the jumps.
  BlackHole hole;
  hole.params[kOuterX] = 4;
  hole.params[kOuterY] = 4;
  hole.params[kInnerX] = 4;
  hole.params[kInnerY] = 4;
  hole.run(2400, 24);  // let the trails settle
  std::vector<uint32_t> before = pixels();
  int largestRise = 0;
  for (int i = 0; i < 400; ++i) {
    hole.frame(24);
    const std::vector<uint32_t> after = pixels();
    for (size_t p = 0; p < after.size(); ++p) {
      const int rise = brightness(after[p]) - brightness(before[p]);
      largestRise = rise > largestRise ? rise : largestRise;
    }
    before = after;
  }
  printf("largest rise of a pixel per step: %d of 255\n", largestRise);
  TEST_ASSERT_TRUE(largestRise < 100);
}

void test_one_color_mode_uses_the_color() {
  // Red stars and a white center: lightening and fading keep green equal to blue.
  BlackHole hole;
  hole.params[kColors] = 1;  // color
  hole.params[kColor] = 0xFF0000;
  hole.params[kBlur] = 1;
  hole.run(2000, 20);
  TEST_ASSERT_TRUE(litPixels() > 0);
  bool red = false;
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(canvas[i].g, canvas[i].b);
    red = red || canvas[i].r > canvas[i].g + 100;
  }
  TEST_ASSERT_TRUE(red);
}

void test_colors_come_from_the_palette() {
  // Every heat color and white have red >= green >= blue. Lightening, fading and
  // blurring keep that order, so no pixel may break it.
  BlackHole hole;
  hole.params[kPalette] = Palettes::indexOf("heat");
  hole.params[kBlur] = 1;
  hole.run(2000, 20);
  TEST_ASSERT_TRUE(litPixels() > 0);
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
    TEST_ASSERT_TRUE(canvas[i].g >= canvas[i].b);
  }
}

void test_blur_spreads_the_light() {
  BlackHole sharp;
  sharp.run(1000, 20);
  const int sharpLit = litPixels();
  canvas.clear();
  BlackHole blurred;
  blurred.params[kBlur] = 1;
  blurred.run(1000, 20);
  TEST_ASSERT_TRUE(litPixels() > sharpLit);
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {2, 2}, {30, 1}, {1, 30}, {9, 9}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
    BlackHole hole;
    hole.params[kFade] = 31;
    hole.params[kOuterX] = 31;
    hole.params[kOuterY] = 0;
    hole.params[kInnerX] = 0;
    hole.params[kInnerY] = 31;
    hole.params[kSize] = 25;
    hole.params[kBlur] = 1;  // smeared below 100 pixels
    hole.run(500, 20);
    TEST_ASSERT_TRUE(litPixels() > 0);
  }
}

void test_long_stall_draws_once() {
  BlackHole hole;
  TEST_ASSERT_TRUE(hole.frame(60000));  // a minute without frames: no hang, one picture
  TEST_ASSERT_TRUE(litPixels() > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_black_hole_is_registered);
  RUN_TEST(test_frame_without_a_step_is_not_drawn);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_center_is_white);
  RUN_TEST(test_stars_move_and_leave_trails);
  RUN_TEST(test_stronger_fade_gives_shorter_trails);
  RUN_TEST(test_larger_stars_light_more_pixels);
  RUN_TEST(test_slow_stars_glide_without_jumps);
  RUN_TEST(test_one_color_mode_uses_the_color);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_blur_spreads_the_light);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_draws_once);
  return UNITY_END();
}

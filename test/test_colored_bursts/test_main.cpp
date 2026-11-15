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

enum P : uint8_t { kSpeed, kLines, kWidth, kBlur, kGradient, kSmear, kDots, kPalette };

Canvas canvas;
Palettes palettes;

// One colored-bursts effect, rendered in frames of the given length.
struct Bursts {
  Bursts() {
    descriptor = EffectRegistry::global().find("colored-bursts");
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

// Smallest channel, the white part of a pixel.
int whiteness(const CRGB& c) {
  return c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
}

int largestWhiteness() {
  int largest = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    largest = whiteness(canvas[i]) > largest ? whiteness(canvas[i]) : largest;
  }
  return largest;
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

void test_colored_bursts_is_registered() {
  const EffectDescriptor* bursts = EffectRegistry::global().find("colored-bursts");
  TEST_ASSERT_NOT_NULL(bursts);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(8, bursts->paramCount);
  TEST_ASSERT_TRUE((bursts->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((bursts->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kLines, ledframe::findParam(*bursts, "lines"));
  TEST_ASSERT_EQUAL_INT(kWidth, ledframe::findParam(*bursts, "width"));
  TEST_ASSERT_EQUAL_INT(kDots, ledframe::findParam(*bursts, "dots"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*bursts, "palette"));
}

void test_frame_without_a_step_is_not_drawn() {
  Bursts bursts;
  TEST_ASSERT_FALSE(bursts.frame(0));   // no time passed
  TEST_ASSERT_FALSE(bursts.frame(20));  // less than one step (24 ms)
  TEST_ASSERT_TRUE(bursts.frame(10));   // 30 ms in total: one step
  TEST_ASSERT_TRUE(litPixels() > 0);
}

void test_same_time_gives_the_same_picture() {
  Bursts first;
  first.run(2000, 20);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Bursts second;
  second.run(2000, 20);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_picture_does_not_depend_on_fps() {
  // Steps run at a fixed rate and the line ends follow the step clock, so the same
  // time gives the same steps at any frame rate.
  Bursts fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Bursts slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_lines_move_even_at_speed_zero() {
  Bursts bursts;
  bursts.params[kSpeed] = 0;  // the base rates (1-5 beats per minute) stay
  bursts.params[kBlur] = 0;
  bursts.run(2000, 20);
  const std::vector<uint32_t> before = pixels();
  bursts.run(2000, 20);
  TEST_ASSERT_FALSE(before == pixels());
}

void test_more_lines_light_more_pixels() {
  Bursts one;
  one.params[kLines] = 1;
  one.params[kBlur] = 0;
  one.run(1000, 20);
  const int fewLit = litPixels();
  canvas.clear();
  Bursts many;
  many.params[kLines] = 16;
  many.params[kBlur] = 0;
  many.run(1000, 20);
  TEST_ASSERT_TRUE(fewLit > 0);
  TEST_ASSERT_TRUE(litPixels() > fewLit);
}

void test_wider_lines_light_more_pixels() {
  Bursts thin;
  thin.params[kLines] = 1;
  thin.params[kBlur] = 0;
  thin.params[kWidth] = 10;
  thin.frame(24);  // one step on a black matrix: only the line itself
  const int thinLit = litPixels();
  canvas.clear();
  Bursts wide;
  wide.params[kLines] = 1;
  wide.params[kBlur] = 0;
  wide.params[kWidth] = 30;
  wide.frame(24);
  TEST_ASSERT_TRUE(thinLit > 0);
  TEST_ASSERT_TRUE(litPixels() > thinLit);
}

void test_slow_lines_glide_without_jumps() {
  // At the lowest speed a line end moves less than 0.1 pixel per step. Drawn pixel by
  // pixel, a line that reaches a new pixel lights it from dark to full in one step; the
  // anti-aliased line brightens it over many steps. Pixels only darken by the trail
  // fade, so the largest rise of a pixel per step shows the jumps.
  Bursts bursts;
  bursts.params[kSpeed] = 0;
  bursts.params[kLines] = 1;
  bursts.params[kBlur] = 0;
  bursts.run(2400, 24);  // let the trail settle
  std::vector<uint32_t> before = pixels();
  int largestRise = 0;
  for (int i = 0; i < 400; ++i) {
    bursts.frame(24);
    const std::vector<uint32_t> after = pixels();
    for (size_t p = 0; p < after.size(); ++p) {
      const int rise = brightness(after[p]) - brightness(before[p]);
      largestRise = rise > largestRise ? rise : largestRise;
    }
    before = after;
  }
  printf("largest rise of a pixel per step: %d of 255\n", largestRise);
  TEST_ASSERT_TRUE(largestRise < 50);
}

void test_gradient_darkens_and_smear_brightens() {
  Bursts plain;
  plain.run(1000, 20);
  const int plainLight = totalLight();
  canvas.clear();
  Bursts gradient;
  gradient.params[kGradient] = 1;
  gradient.run(1000, 20);
  const int gradientLight = totalLight();
  canvas.clear();
  Bursts smear;
  smear.params[kSmear] = 1;
  smear.run(1000, 20);
  TEST_ASSERT_TRUE(gradientLight < plainLight);
  TEST_ASSERT_TRUE(totalLight() > plainLight);
}

void test_dots_mark_the_line_ends() {
  // The white start dot covers the pixels around the start point, so the picture gets
  // whiter than the same step without dots.
  Bursts plain;
  plain.params[kBlur] = 0;
  plain.frame(24);  // one step
  const int plainWhite = largestWhiteness();
  canvas.clear();
  Bursts dotted;
  dotted.params[kDots] = 1;
  dotted.params[kBlur] = 0;
  dotted.frame(24);
  TEST_ASSERT_TRUE(largestWhiteness() >= 128);
  TEST_ASSERT_TRUE(largestWhiteness() > plainWhite);
}

void test_colors_come_from_the_palette() {
  // Every heat color has red >= green >= blue. Lightening, dimming and blurring keep
  // that order, so no pixel may break it.
  Bursts bursts;
  bursts.params[kPalette] = Palettes::indexOf("heat");
  bursts.params[kGradient] = 1;
  bursts.run(2000, 20);
  TEST_ASSERT_TRUE(litPixels() > 0);
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
    TEST_ASSERT_TRUE(canvas[i].g >= canvas[i].b);
  }
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {30, 1}, {1, 30}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
    Bursts bursts;
    bursts.params[kSpeed] = 15;
    bursts.params[kLines] = 16;
    bursts.params[kWidth] = 30;
    bursts.params[kBlur] = 15;
    bursts.params[kGradient] = 1;
    bursts.params[kSmear] = 1;
    bursts.params[kDots] = 1;
    bursts.run(500, 20);
    TEST_ASSERT_TRUE(litPixels() > 0);
  }
}

void test_long_stall_draws_once() {
  Bursts bursts;
  TEST_ASSERT_TRUE(bursts.frame(60000));  // a minute without frames: no hang, one picture
  TEST_ASSERT_TRUE(litPixels() > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_colored_bursts_is_registered);
  RUN_TEST(test_frame_without_a_step_is_not_drawn);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_lines_move_even_at_speed_zero);
  RUN_TEST(test_more_lines_light_more_pixels);
  RUN_TEST(test_wider_lines_light_more_pixels);
  RUN_TEST(test_slow_lines_glide_without_jumps);
  RUN_TEST(test_gradient_darkens_and_smear_brightens);
  RUN_TEST(test_dots_mark_the_line_ends);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_draws_once);
  return UNITY_END();
}

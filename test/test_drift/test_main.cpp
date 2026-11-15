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

enum P : uint8_t { kSpeed, kBlur, kTwin, kSmear, kSize, kPalette };

Canvas canvas;
Palettes palettes;

// One drift, rendered in frames of the given length.
struct Drift {
  Drift() {
    descriptor = EffectRegistry::global().find("drift");
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

int litPixels() {
  int count = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    count += (canvas[i].r | canvas[i].g | canvas[i].b) != 0 ? 1 : 0;
  }
  return count;
}

int totalLight() {
  int sum = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    sum += canvas[i].r + canvas[i].g + canvas[i].b;
  }
  return sum;
}

std::vector<uint32_t> picture(uint32_t totalMs, uint8_t index, int32_t value) {
  canvas.clear();
  Drift drift;
  drift.params[index] = value;
  drift.run(totalMs, 20);
  return pixels();
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_drift_is_registered() {
  const EffectDescriptor* drift = EffectRegistry::global().find("drift");
  TEST_ASSERT_NOT_NULL(drift);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(6, drift->paramCount);
  TEST_ASSERT_TRUE((drift->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((drift->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kBlur, ledframe::findParam(*drift, "blur"));
  TEST_ASSERT_EQUAL_INT(kTwin, ledframe::findParam(*drift, "twin"));
  TEST_ASSERT_EQUAL_INT(kSize, ledframe::findParam(*drift, "size"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*drift, "palette"));
}

void test_frame_without_a_step_is_not_drawn() {
  Drift drift;
  TEST_ASSERT_FALSE(drift.frame(0));   // no time passed
  TEST_ASSERT_FALSE(drift.frame(20));  // less than one step (24 ms)
  TEST_ASSERT_TRUE(drift.frame(10));   // 30 ms in total: one step
  TEST_ASSERT_TRUE(litPixels() > 0);
}

void test_same_time_gives_the_same_picture() {
  TEST_ASSERT_TRUE(picture(2000, kSpeed, 16) == picture(2000, kSpeed, 16));
}

void test_picture_does_not_depend_on_fps() {
  Drift fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Drift slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_arm_turns_around_the_middle() {
  // 15 x 10: the arm reaches from radius 1 to 6.75 around (7, 4.5). The center pixels
  // stay dark (no point closer than 1 pixel and size 1.0), the arm lights others.
  Drift drift;
  drift.run(2000, 20);
  TEST_ASSERT_TRUE(litPixels() >= 10);
  const std::vector<uint32_t> before = pixels();
  drift.run(500, 20);
  TEST_ASSERT_FALSE(before == pixels());
}

void test_speed_turns_faster() {
  // Faster rotation spreads the points of one step over more of their circles, and
  // the picture after the same time differs.
  TEST_ASSERT_FALSE(picture(2000, kSpeed, 0) == picture(2000, kSpeed, 31));
}

void test_twin_adds_a_mirrored_arm() {
  Drift one;
  one.frame(24);  // one step on a black matrix: only the points
  const int single = litPixels();
  canvas.clear();
  Drift two;
  two.params[kTwin] = 1;
  two.frame(24);
  TEST_ASSERT_TRUE(litPixels() > single);
}

void test_blur_and_smear() {
  const std::vector<uint32_t> sharp = picture(1000, kBlur, 0);
  int sharpLit = 0;
  for (uint32_t p : sharp) {
    sharpLit += p != 0 ? 1 : 0;
  }
  picture(1000, kBlur, 31);
  const int blurredLit = litPixels();
  const int blurredLight = totalLight();
  TEST_ASSERT_TRUE(blurredLit > sharpLit);

  canvas.clear();
  Drift smeared;
  smeared.params[kBlur] = 31;
  smeared.params[kSmear] = 1;
  smeared.run(1000, 20);
  TEST_ASSERT_TRUE(totalLight() > blurredLight);  // smear keeps every pixel
}

void test_larger_points_light_more_pixels() {
  Drift small;
  small.frame(24);
  const int smallLit = litPixels();
  canvas.clear();
  Drift large;
  large.params[kSize] = 20;
  large.frame(24);
  TEST_ASSERT_TRUE(litPixels() > smallLit);
}

void test_points_glide_without_jumps() {
  // The points move in fractions of a pixel and are drawn as soft dots, so a pixel that
  // a point reaches brightens over several steps. Put into whole pixels, a point lights
  // a new pixel from dark to full in one step. Pixels only darken by the trail fade, so
  // the largest rise of a pixel per step shows the jumps.
  Drift drift;
  drift.params[kSpeed] = 8;
  drift.run(2400, 24);
  std::vector<uint32_t> before = pixels();
  int largestRise = 0;
  for (int i = 0; i < 400; ++i) {
    drift.frame(24);
    const std::vector<uint32_t> after = pixels();
    for (size_t p = 0; p < after.size(); ++p) {
      const int rise = brightness(after[p]) - brightness(before[p]);
      largestRise = rise > largestRise ? rise : largestRise;
    }
    before = after;
  }
  printf("largest rise of a pixel per step: %d of 255\n", largestRise);
  TEST_ASSERT_TRUE(largestRise < 120);  // 82 here, 255 drawn into whole pixels
}

void test_colors_come_from_the_palette() {
  // Every heat color has red >= green >= blue. Lightening, fading and blurring keep
  // that order, so no pixel may break it.
  for (int32_t smear : {0, 1}) {
    canvas.clear();
    Drift drift;
    drift.params[kPalette] = Palettes::indexOf("heat");
    drift.params[kTwin] = 1;
    drift.params[kBlur] = 20;
    drift.params[kSmear] = smear;
    drift.run(2000, 20);
    TEST_ASSERT_TRUE(litPixels() > 0);
    for (int i = 0; i < canvas.size(); ++i) {
      TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
      TEST_ASSERT_TRUE(canvas[i].g >= canvas[i].b);
    }
  }
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
    bool lit;  // below 4 pixels on the longer side the arm has no points, as in WLED
  } sizes[] = {{1, 1, false}, {3, 3, false},  {4, 1, true},   {30, 1, true},
               {1, 30, true}, {32, 32, true}, {1024, 1, true}};
  for (const auto& size : sizes) {
    TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
    Drift drift;
    drift.params[kSpeed] = 31;
    drift.params[kBlur] = 31;
    drift.params[kTwin] = 1;
    drift.params[kSmear] = 1;
    drift.params[kSize] = 20;
    drift.run(500, 20);
    TEST_ASSERT_EQUAL(size.lit, litPixels() > 0);
  }
}

void test_long_stall_draws_once() {
  Drift drift;
  TEST_ASSERT_TRUE(drift.frame(60000));  // a minute without frames: no hang, one picture
  TEST_ASSERT_TRUE(litPixels() > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_drift_is_registered);
  RUN_TEST(test_frame_without_a_step_is_not_drawn);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_arm_turns_around_the_middle);
  RUN_TEST(test_speed_turns_faster);
  RUN_TEST(test_twin_adds_a_mirrored_arm);
  RUN_TEST(test_blur_and_smear);
  RUN_TEST(test_larger_points_light_more_pixels);
  RUN_TEST(test_points_glide_without_jumps);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_draws_once);
  return UNITY_END();
}

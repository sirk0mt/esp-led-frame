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

enum P : uint8_t { kSpeed, kXScale, kYScale, kPalette };

Canvas canvas;
Palettes palettes;

// One hiphotic effect, rendered in frames of the given length.
struct Hiphotic {
  Hiphotic() {
    descriptor = EffectRegistry::global().find("hiphotic");
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
  void change(uint8_t index, int32_t value) {
    params[index] = value;
    FrameContext ctx = context(0);
    effect->onParamChanged(index, ctx);
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

// Largest change of one channel of one pixel between two pictures.
int largestChange(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  int largest = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    for (int shift = 0; shift <= 16; shift += 8) {
      const int before = (a[i] >> shift) & 0xFF;
      const int after = (b[i] >> shift) & 0xFF;
      const int change = after > before ? after - before : before - after;
      largest = change > largest ? change : largest;
    }
  }
  return largest;
}

std::vector<uint32_t> picture(uint32_t totalMs, uint8_t index = kSpeed, int32_t value = 11) {
  canvas.clear();
  Hiphotic hiphotic;
  hiphotic.params[index] = value;
  hiphotic.run(totalMs, 20);
  return pixels();
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_hiphotic_is_registered() {
  const EffectDescriptor* hiphotic = EffectRegistry::global().find("hiphotic");
  TEST_ASSERT_NOT_NULL(hiphotic);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, hiphotic->paramCount);
  TEST_ASSERT_TRUE((hiphotic->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((hiphotic->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kXScale, ledframe::findParam(*hiphotic, "x_scale"));
  TEST_ASSERT_EQUAL_INT(kYScale, ledframe::findParam(*hiphotic, "y_scale"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*hiphotic, "palette"));
}

void test_same_time_gives_the_same_picture() {
  TEST_ASSERT_TRUE(picture(2000) == picture(2000));
}

void test_picture_does_not_depend_on_fps() {
  Hiphotic fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Hiphotic slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_speed_scales_the_pace() {
  // Twice the speed for half the time gives the same picture.
  TEST_ASSERT_TRUE(picture(1000, kSpeed, 22) == picture(2000, kSpeed, 11));
  TEST_ASSERT_FALSE(picture(1000, kSpeed, 11) == picture(2000, kSpeed, 11));
}

void test_speed_zero_is_still() {
  Hiphotic hiphotic;
  hiphotic.params[kSpeed] = 0;
  TEST_ASSERT_TRUE(hiphotic.frame(20));  // the first picture
  const std::vector<uint32_t> still = pixels();
  TEST_ASSERT_FALSE(hiphotic.frame(20));  // nothing changed: no frame
  TEST_ASSERT_FALSE(hiphotic.frame(20));
  hiphotic.change(kXScale, 40);
  TEST_ASSERT_TRUE(hiphotic.frame(20));  // a new parameter draws again
  TEST_ASSERT_FALSE(still == pixels());
}

void test_scales_shape_the_waves() {
  // Without a y scale every row is the same, without an x scale every column; with
  // both the picture changes along both axes.
  picture(1500, kYScale, 0);
  bool rowsDiffer = false;
  bool columnsDiffer = false;
  for (int y = 1; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      rowsDiffer = rowsDiffer || !(canvas.at(x, y) == canvas.at(x, 0));
      columnsDiffer = columnsDiffer || !(canvas.at(x, y) == canvas.at(0, y));
    }
  }
  TEST_ASSERT_FALSE(rowsDiffer);
  TEST_ASSERT_TRUE(columnsDiffer);

  picture(1500, kXScale, 0);
  rowsDiffer = false;
  columnsDiffer = false;
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 1; x < canvas.width(); ++x) {
      rowsDiffer = rowsDiffer || !(canvas.at(x, y) == canvas.at(x, 0));
      columnsDiffer = columnsDiffer || !(canvas.at(x, y) == canvas.at(0, y));
    }
  }
  TEST_ASSERT_TRUE(rowsDiffer);
  TEST_ASSERT_FALSE(columnsDiffer);

  TEST_ASSERT_FALSE(picture(1500) == picture(1500, kXScale, 200));
  TEST_ASSERT_FALSE(picture(1500) == picture(1500, kYScale, 200));
}

void test_colors_flow_smoothly() {
  // The angles and the palette position have 16 bits and time runs on in fractions, so
  // every frame changes the picture a little. With WLED's 8-bit steps the picture at
  // speed 1 stands still in 200 of 250 frames and then a channel jumps by up to 81.
  for (int32_t speed : {1, 11, 100}) {
    Hiphotic hiphotic;
    hiphotic.params[kSpeed] = speed;
    hiphotic.run(1000, 20);
    std::vector<uint32_t> before = pixels();
    int largest = 0;
    int still = 0;
    for (int i = 0; i < 250; ++i) {
      hiphotic.frame(20);
      const std::vector<uint32_t> after = pixels();
      const int change = largestChange(before, after);
      largest = change > largest ? change : largest;
      still += change == 0 ? 1 : 0;
      before = after;
    }
    printf("speed %d: largest change per 20 ms frame %d, frames without change %d of 250\n",
           static_cast<int>(speed), largest, still);
    TEST_ASSERT_TRUE(still < 25);
    if (speed == 1) {
      TEST_ASSERT_TRUE(largest < 40);  // 15 here; 81 with WLED's 8-bit steps
    }
  }
}

void test_colors_come_from_the_palette() {
  // Every heat color has red >= green >= blue, and so has every blend of two.
  canvas.clear();
  Hiphotic hiphotic;
  hiphotic.params[kPalette] = Palettes::indexOf("heat");
  for (int i = 0; i < 200; ++i) {
    hiphotic.frame(20);
    for (int p = 0; p < canvas.size(); ++p) {
      TEST_ASSERT_TRUE(canvas[p].r >= canvas[p].g);
      TEST_ASSERT_TRUE(canvas[p].g >= canvas[p].b);
    }
  }
  // The rainbow palette shows many colors at once.
  picture(1500);
  bool red = false;
  bool green = false;
  bool blue = false;
  for (int p = 0; p < canvas.size(); ++p) {
    red = red || canvas[p].r > 128;
    green = green || canvas[p].g > 128;
    blue = blue || canvas[p].b > 128;
  }
  TEST_ASSERT_TRUE(red && green && blue);
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {30, 1}, {1, 30}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    for (int32_t scale : {0, 255}) {
      TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
      Hiphotic hiphotic;
      hiphotic.params[kSpeed] = 100;
      hiphotic.params[kXScale] = scale;
      hiphotic.params[kYScale] = scale;
      TEST_ASSERT_TRUE(hiphotic.frame(20));
      hiphotic.run(500, 20);
    }
  }
}

void test_long_stall_moves_on_at_most_a_second() {
  Hiphotic stalled;
  stalled.frame(20);
  TEST_ASSERT_TRUE(stalled.frame(60000));  // a minute without frames: one second on
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Hiphotic steady;
  steady.frame(20);
  steady.frame(1000);
  TEST_ASSERT_TRUE(a == pixels());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hiphotic_is_registered);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_speed_scales_the_pace);
  RUN_TEST(test_speed_zero_is_still);
  RUN_TEST(test_scales_shape_the_waves);
  RUN_TEST(test_colors_flow_smoothly);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_moves_on_at_most_a_second);
  return UNITY_END();
}

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

enum P : uint8_t { kSpeed, kScale, kZoom, kAlt, kColors, kPalette };
enum Colors : int32_t { kColorsRgb, kColorsPalette, kColorsFill };

Canvas canvas;
Palettes palettes;

// One distortion-waves effect, rendered in frames of the given length.
struct Waves {
  Waves() {
    descriptor = EffectRegistry::global().find("distortion-waves");
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

std::vector<uint32_t> picture(uint32_t totalMs, uint8_t index = kSpeed, int32_t value = 10) {
  canvas.clear();
  Waves waves;
  waves.params[index] = value;
  waves.run(totalMs, 20);
  return pixels();
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_distortion_waves_is_registered() {
  const EffectDescriptor* waves = EffectRegistry::global().find("distortion-waves");
  TEST_ASSERT_NOT_NULL(waves);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(6, waves->paramCount);
  TEST_ASSERT_TRUE((waves->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((waves->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kScale, ledframe::findParam(*waves, "scale"));
  TEST_ASSERT_EQUAL_INT(kAlt, ledframe::findParam(*waves, "alt"));
  TEST_ASSERT_EQUAL_INT(kColors, ledframe::findParam(*waves, "colors"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*waves, "palette"));
}

void test_same_time_gives_the_same_picture() {
  TEST_ASSERT_TRUE(picture(2000) == picture(2000));
}

void test_picture_does_not_depend_on_fps() {
  Waves fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Waves slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_speed_scales_the_pace() {
  // Twice the speed for half the time gives the same picture.
  TEST_ASSERT_TRUE(picture(1000, kSpeed, 20) == picture(2000, kSpeed, 10));
  TEST_ASSERT_FALSE(picture(1000, kSpeed, 10) == picture(2000, kSpeed, 10));
}

void test_speed_zero_is_still() {
  Waves waves;
  waves.params[kSpeed] = 0;
  TEST_ASSERT_TRUE(waves.frame(20));  // the first picture
  const std::vector<uint32_t> still = pixels();
  TEST_ASSERT_FALSE(waves.frame(20));  // nothing changed: no frame
  TEST_ASSERT_FALSE(waves.frame(20));
  waves.change(kAlt, 1);
  TEST_ASSERT_TRUE(waves.frame(20));  // a new parameter draws again
  TEST_ASSERT_FALSE(still == pixels());
}

void test_waves_flow_smoothly() {
  // Time runs on in fractions and the angles have 16 bits, so every frame changes the
  // picture a little. With WLED's time in steps of 32 ms and angles in 1/256 turns, a
  // channel jumped by up to 26 per frame at speed 1, against 4 here.
  for (int32_t speed : {1, 10, 30}) {
    Waves waves;
    waves.params[kSpeed] = speed;
    waves.run(1000, 20);
    std::vector<uint32_t> before = pixels();
    int largest = 0;
    int still = 0;
    for (int i = 0; i < 250; ++i) {
      waves.frame(20);
      const std::vector<uint32_t> after = pixels();
      const int change = largestChange(before, after);
      largest = change > largest ? change : largest;
      still += change == 0 ? 1 : 0;
      before = after;
    }
    printf("speed %d: largest change per 20 ms frame %d, frames without change %d of 250\n",
           static_cast<int>(speed), largest, still);
    TEST_ASSERT_TRUE(largest <= 2 + 10 * speed);
    TEST_ASSERT_TRUE(still < 25);
  }
}

void test_rgb_colors_mix_all_channels() {
  picture(1500);
  bool red = false;
  bool green = false;
  bool blue = false;
  for (int i = 0; i < canvas.size(); ++i) {
    red = red || canvas[i].r > 128;
    green = green || canvas[i].g > 128;
    blue = blue || canvas[i].b > 128;
  }
  TEST_ASSERT_TRUE(red && green && blue);
  // Not one color: the waves change across the matrix.
  TEST_ASSERT_FALSE(canvas.at(0, 0) == canvas.at(14, 9) && canvas.at(0, 0) == canvas.at(7, 4));
}

void test_palette_modes_use_the_palette() {
  // Every heat color has red >= green >= blue. Scaling and the smeared blur keep that
  // order, so no pixel may break it in either palette mode.
  for (int32_t colors : {static_cast<int32_t>(kColorsPalette), static_cast<int32_t>(kColorsFill)}) {
    canvas.clear();
    Waves waves;
    waves.params[kColors] = colors;
    waves.params[kPalette] = Palettes::indexOf("heat");
    waves.run(1500, 20);
    int lit = 0;
    for (int i = 0; i < canvas.size(); ++i) {
      TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
      TEST_ASSERT_TRUE(canvas[i].g >= canvas[i].b);
      lit += canvas[i].r > 0 ? 1 : 0;
    }
    TEST_ASSERT_TRUE(lit > 0);
  }
}

void test_options_change_the_picture() {
  const std::vector<uint32_t> plain = picture(1500);
  TEST_ASSERT_FALSE(plain == picture(1500, kAlt, 1));
  TEST_ASSERT_FALSE(plain == picture(1500, kZoom, 1));
  TEST_ASSERT_FALSE(plain == picture(1500, kScale, 1));
  TEST_ASSERT_FALSE(plain == picture(1500, kColors, kColorsFill));
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {30, 1}, {1, 30}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    for (int32_t scale : {0, 7}) {
      TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
      Waves waves;
      waves.params[kSpeed] = 30;
      waves.params[kScale] = scale;
      waves.params[kZoom] = 1;
      waves.params[kColors] = kColorsPalette;
      TEST_ASSERT_TRUE(waves.frame(20));
      waves.run(500, 20);
    }
  }
}

void test_long_stall_moves_on_at_most_a_second() {
  Waves stalled;
  stalled.frame(20);
  TEST_ASSERT_TRUE(stalled.frame(60000));  // a minute without frames: one second on
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Waves steady;
  steady.frame(20);
  steady.frame(1000);
  TEST_ASSERT_TRUE(a == pixels());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_distortion_waves_is_registered);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_speed_scales_the_pace);
  RUN_TEST(test_speed_zero_is_still);
  RUN_TEST(test_waves_flow_smoothly);
  RUN_TEST(test_rgb_colors_mix_all_channels);
  RUN_TEST(test_palette_modes_use_the_palette);
  RUN_TEST(test_options_change_the_picture);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_moves_on_at_most_a_second);
  return UNITY_END();
}

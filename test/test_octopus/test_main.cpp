#include <unity.h>

#include <cmath>
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

enum P : uint8_t { kSpeed, kLegs, kLegSize, kOffsetX, kOffsetY, kPalette };

Canvas canvas;
Palettes palettes;

// One octopus, rendered in frames of the given length. The parameter given here is set
// before begin(), as when the engine creates the effect.
struct Octopus {
  explicit Octopus(uint8_t index = kSpeed, int32_t value = 20) {
    descriptor = EffectRegistry::global().find("octopus");
    ledframe::loadDefaults(*descriptor, params);
    params[index] = value;
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

std::vector<uint32_t> picture(uint32_t totalMs, uint8_t index = kSpeed, int32_t value = 20) {
  canvas.clear();
  Octopus octopus(index, value);
  octopus.run(totalMs, 20);
  return pixels();
}

// Bright arms crossed on a circle of the given radius around the middle of the matrix:
// rises of the light (r + g + b) from below 60 to above 200. The walk starts in a gap
// between two arms, so every arm is counted once.
int armsOnCircle(float radius) {
  const float centerX = static_cast<float>(canvas.width() - 1) / 2.0f;
  const float centerY = static_cast<float>(canvas.height() - 1) / 2.0f;
  const int steps = 720;
  auto light = [&](int i) {
    const float a = static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(steps);
    const CRGB& c = canvas.at(static_cast<int>(std::lround(centerX + radius * std::cos(a))),
                              static_cast<int>(std::lround(centerY + radius * std::sin(a))));
    return c.r + c.g + c.b;
  };
  int start = 0;
  while (start < steps && light(start) >= 60) {
    ++start;
  }
  int arms = 0;
  bool low = true;
  for (int i = start; i < start + steps; ++i) {
    const int value = light(i % steps);
    if (value < 60) {
      low = true;
    } else if (value > 200 && low) {
      low = false;
      ++arms;
    }
  }
  return arms;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_octopus_is_registered() {
  const EffectDescriptor* octopus = EffectRegistry::global().find("octopus");
  TEST_ASSERT_NOT_NULL(octopus);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(6, octopus->paramCount);
  TEST_ASSERT_TRUE((octopus->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((octopus->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kLegs, ledframe::findParam(*octopus, "legs"));
  TEST_ASSERT_EQUAL_INT(kLegSize, ledframe::findParam(*octopus, "leg_size"));
  TEST_ASSERT_EQUAL_INT(kOffsetX, ledframe::findParam(*octopus, "offset_x"));
  TEST_ASSERT_EQUAL_INT(kOffsetY, ledframe::findParam(*octopus, "offset_y"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*octopus, "palette"));
}

void test_same_time_gives_the_same_picture() {
  TEST_ASSERT_TRUE(picture(2000) == picture(2000));
}

void test_picture_does_not_depend_on_fps() {
  Octopus fast;
  fast.run(1200, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Octopus slow;
  slow.run(1200, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_speed_scales_the_pace() {
  // Twice the speed for half the time gives the same picture.
  TEST_ASSERT_TRUE(picture(1000, kSpeed, 20) == picture(2000, kSpeed, 10));
  TEST_ASSERT_FALSE(picture(1000, kSpeed, 10) == picture(2000, kSpeed, 10));
}

void test_speed_zero_is_still() {
  Octopus octopus(kSpeed, 0);
  TEST_ASSERT_TRUE(octopus.frame(20));  // the first picture
  const std::vector<uint32_t> still = pixels();
  TEST_ASSERT_FALSE(octopus.frame(20));  // nothing changed: no frame
  TEST_ASSERT_FALSE(octopus.frame(20));
  octopus.change(kLegs, 3);
  TEST_ASSERT_TRUE(octopus.frame(20));  // a new parameter draws again
  TEST_ASSERT_FALSE(still == pixels());
}

void test_legs_sets_the_number_of_arms() {
  // Around the center the brightness goes through `legs` waves (from 4 legs on, the
  // bending sine cannot add any).
  for (int32_t legs : {4, 5, 8}) {
    TEST_ASSERT_TRUE(canvas.resize(32, 32));
    Octopus octopus(kLegs, legs);
    octopus.run(1000, 20);
    const int arms = armsOnCircle(12.0f);
    printf("legs %d: %d arms on a circle\n", static_cast<int>(legs), arms);
    TEST_ASSERT_EQUAL_INT(legs, arms);
  }
}

void test_leg_size_sets_the_width_of_the_arms() {
  // Thin arms leave most of the matrix dark, thick arms most of it bright; the number
  // of arms stays.
  int dark[3] = {};
  int full[3] = {};
  const int32_t sizes[3] = {10, 50, 90};
  for (int s = 0; s < 3; ++s) {
    TEST_ASSERT_TRUE(canvas.resize(32, 32));
    Octopus octopus(kLegSize, sizes[s]);
    octopus.run(1000, 20);
    for (int p = 0; p < canvas.size(); ++p) {
      const int light = canvas[p].r + canvas[p].g + canvas[p].b;
      dark[s] += light < 20 ? 1 : 0;
      full[s] += light > 235 ? 1 : 0;
    }
    TEST_ASSERT_EQUAL_INT(5, armsOnCircle(12.0f));
  }
  TEST_ASSERT_TRUE(dark[0] > 2 * dark[1]);
  TEST_ASSERT_TRUE(dark[1] > dark[2]);
  TEST_ASSERT_TRUE(full[2] > 2 * full[1]);
  TEST_ASSERT_TRUE(full[1] > full[0]);
  TEST_ASSERT_TRUE(dark[0] > canvas.size() / 2);  // size 10: more than half dark
  TEST_ASSERT_TRUE(full[2] > canvas.size() / 2);  // size 90: more than half at full light
}

void test_offsets_move_the_center() {
  const std::vector<uint32_t> middle = picture(1000);
  TEST_ASSERT_FALSE(middle == picture(1000, kOffsetX, 60));
  TEST_ASSERT_FALSE(middle == picture(1000, kOffsetY, 200));
  // Moving the center while running rebuilds the map: the same picture as with the
  // center there from the start.
  canvas.clear();
  Octopus moved;
  moved.run(500, 20);
  moved.change(kOffsetX, 60);
  moved.run(500, 20);
  const std::vector<uint32_t> after = pixels();
  TEST_ASSERT_TRUE(picture(1000, kOffsetX, 60) == after);
}

void test_arms_flow_smoothly() {
  // The phases and the palette position have 16 bits and time runs on in fractions, so
  // every frame changes the picture a little: at speed 1 by at most 3 per channel. With
  // WLED's 8-bit steps the picture stands still in 197 of 250 frames and then jumps by
  // up to 14.
  for (int32_t speed : {1, 20, 32}) {
    canvas.clear();
    Octopus octopus(kSpeed, speed);
    octopus.run(1000, 20);
    std::vector<uint32_t> before = pixels();
    int largest = 0;
    int still = 0;
    for (int i = 0; i < 250; ++i) {
      octopus.frame(20);
      const std::vector<uint32_t> after = pixels();
      const int change = largestChange(before, after);
      largest = change > largest ? change : largest;
      still += change == 0 ? 1 : 0;
      before = after;
    }
    printf("speed %d: largest change per 20 ms frame %d, frames without change %d of 250\n",
           static_cast<int>(speed), largest, still);
    TEST_ASSERT_TRUE(still < 25);
  }
}

void test_colors_come_from_the_palette() {
  // Every heat color has red >= green >= blue, and so has every dimmed blend of two.
  canvas.clear();
  Octopus octopus(kPalette, Palettes::indexOf("heat"));
  for (int i = 0; i < 200; ++i) {
    octopus.frame(20);
    for (int p = 0; p < canvas.size(); ++p) {
      TEST_ASSERT_TRUE(canvas[p].r >= canvas[p].g);
      TEST_ASSERT_TRUE(canvas[p].g >= canvas[p].b);
    }
  }
  // Arms and gaps: both bright and dark pixels.
  picture(1500);
  bool bright = false;
  bool dark = false;
  for (int p = 0; p < canvas.size(); ++p) {
    const int light = canvas[p].r + canvas[p].g + canvas[p].b;
    bright = bright || light > 200;
    dark = dark || light < 30;
  }
  TEST_ASSERT_TRUE(bright && dark);
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {2, 2}, {30, 1}, {1, 30}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    for (int32_t offset : {0, 255}) {
      TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
      Octopus octopus(kOffsetX, offset);
      octopus.change(kOffsetY, 255 - offset);
      octopus.change(kLegs, 8);
      octopus.change(kLegSize, offset == 0 ? 0 : 100);
      octopus.change(kSpeed, 32);
      TEST_ASSERT_TRUE(octopus.frame(20));
      octopus.run(500, 20);
    }
  }
}

void test_long_stall_moves_on_at_most_a_second() {
  Octopus stalled;
  stalled.frame(20);
  TEST_ASSERT_TRUE(stalled.frame(60000));  // a minute without frames: one second on
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Octopus steady;
  steady.frame(20);
  steady.frame(1000);
  TEST_ASSERT_TRUE(a == pixels());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_octopus_is_registered);
  RUN_TEST(test_same_time_gives_the_same_picture);
  RUN_TEST(test_picture_does_not_depend_on_fps);
  RUN_TEST(test_speed_scales_the_pace);
  RUN_TEST(test_speed_zero_is_still);
  RUN_TEST(test_legs_sets_the_number_of_arms);
  RUN_TEST(test_leg_size_sets_the_width_of_the_arms);
  RUN_TEST(test_offsets_move_the_center);
  RUN_TEST(test_arms_flow_smoothly);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_moves_on_at_most_a_second);
  return UNITY_END();
}

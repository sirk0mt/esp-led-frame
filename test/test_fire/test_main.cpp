#include <lib8tion.h>
#include <unity.h>

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

enum P : uint8_t { kSpeed, kCooling, kSparking, kPalette };

Canvas canvas;
Palettes palettes;

// One fire with a fixed random seed, rendered in frames of stepMs.
struct Fire {
  Fire() {
    descriptor = EffectRegistry::global().find("fire");
    ledframe::loadDefaults(*descriptor, params);
    random16_set_seed(1234);
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
  void run(uint32_t totalMs, uint32_t stepMs) {
    for (uint32_t elapsed = 0; elapsed < totalMs; elapsed += stepMs) {
      frame(stepMs);
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

int rowLight(int y) {
  int sum = 0;
  for (int x = 0; x < canvas.width(); ++x) {
    sum += canvas.at(x, y).r + canvas.at(x, y).g + canvas.at(x, y).b;
  }
  return sum;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_fire_is_registered_with_the_heat_palette() {
  const EffectDescriptor* fire = EffectRegistry::global().find("fire");
  TEST_ASSERT_NOT_NULL(fire);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(4, fire->paramCount);
  TEST_ASSERT_TRUE((fire->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*fire, "palette"));
  TEST_ASSERT_EQUAL_INT(Palettes::indexOf("heat"), ledframe::defaultParam(fire->params[kPalette]));
}

void test_fire_is_deterministic_for_a_fixed_seed() {
  Fire first;
  first.run(2000, 20);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Fire second;
  second.run(2000, 20);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_fire_does_not_depend_on_fps() {
  // Steps run at a fixed rate, so the same time gives the same steps with the same
  // random numbers at any frame rate.
  Fire fast;
  fast.run(1000, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Fire slow;
  slow.run(1000, 50);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_fire_is_hot_at_the_bottom() {
  Fire fire;
  fire.run(3000, 20);
  TEST_ASSERT_TRUE(rowLight(9) > 0);
  TEST_ASSERT_TRUE(rowLight(9) > rowLight(0));
  TEST_ASSERT_TRUE(rowLight(8) > rowLight(1));
}

void test_fire_uses_heat_colors() {
  Fire fire;
  fire.run(2000, 20);
  // The heat palette goes black, red, yellow, white: red never trails green or blue.
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
    TEST_ASSERT_TRUE(canvas[i].g >= canvas[i].b);
  }
}

void test_frame_without_a_step_is_not_drawn() {
  Fire fire;
  TEST_ASSERT_FALSE(fire.frame(0));    // no time passed
  TEST_ASSERT_FALSE(fire.frame(10));   // less than one step (60 steps/s: 16 ms)
  TEST_ASSERT_TRUE(fire.frame(10));    // 20 ms in total: one step
}

void test_one_row_matrix_works() {
  TEST_ASSERT_TRUE(canvas.resize(30, 1));
  Fire fire;
  fire.params[kCooling] = 100;  // the largest cooling on the smallest height
  fire.run(1000, 20);
  TEST_ASSERT_TRUE(rowLight(0) >= 0);  // no crash, colors still from the palette
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_TRUE(canvas[i].r >= canvas[i].g);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fire_is_registered_with_the_heat_palette);
  RUN_TEST(test_fire_is_deterministic_for_a_fixed_seed);
  RUN_TEST(test_fire_does_not_depend_on_fps);
  RUN_TEST(test_fire_is_hot_at_the_bottom);
  RUN_TEST(test_fire_uses_heat_colors);
  RUN_TEST(test_frame_without_a_step_is_not_drawn);
  RUN_TEST(test_one_row_matrix_works);
  return UNITY_END();
}

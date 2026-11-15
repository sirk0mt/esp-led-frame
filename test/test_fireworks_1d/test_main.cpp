#include <lib8tion.h>
#include <unity.h>

#include <algorithm>
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

enum P : uint8_t { kGravity, kSide, kColors, kColor, kSize, kPalette };

Canvas canvas;
Palettes palettes;

// One fireworks effect with a fixed random seed, rendered in frames of the given length.
struct Fireworks {
  explicit Fireworks(uint16_t seed = 1234) {
    descriptor = EffectRegistry::global().find("fireworks-1d");
    ledframe::loadDefaults(*descriptor, params);
    random16_set_seed(seed);
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

bool lit(const CRGB& c) {
  return (c.r | c.g | c.b) != 0;
}

int litPixels() {
  int count = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    count += lit(canvas[i]) ? 1 : 0;
  }
  return count;
}

// A pixel clearly in a color, not white or gray.
bool colored(const CRGB& c) {
  const int top = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
  const int bottom = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
  return top - bottom > 60;
}

bool anyColored() {
  for (int i = 0; i < canvas.size(); ++i) {
    if (colored(canvas[i])) {
      return true;
    }
  }
  return false;
}

// Spread of the colored light (the sparks, not the white rocket): its standard
// deviation across and along, in pixels.
struct Spread {
  float x;
  float y;
};

Spread coloredSpread() {
  double sum = 0;
  double sumX = 0;
  double sumY = 0;
  double sumXX = 0;
  double sumYY = 0;
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      const CRGB& c = canvas.at(x, y);
      if (!colored(c)) {
        continue;
      }
      const double w = c.r + c.g + c.b;
      sum += w;
      sumX += w * x;
      sumY += w * y;
      sumXX += w * x * x;
      sumYY += w * y * y;
    }
  }
  if (sum == 0) {
    return {0.0f, 0.0f};
  }
  const double mx = sumX / sum;
  const double my = sumY / sum;
  return {static_cast<float>(std::sqrt(sumXX / sum - mx * mx)),
          static_cast<float>(std::sqrt(sumYY / sum - my * my))};
}

// The first burst of a seed on a 32 x 32 matrix, 20 steps after its first colored
// spark: the shell has spread but not yet faded.
Spread firstBurst(uint16_t seed) {
  TEST_ASSERT_TRUE(canvas.resize(32, 32));
  Fireworks fireworks(seed);
  for (int i = 0; i < 1000 && !anyColored(); ++i) {
    fireworks.frame(24);
  }
  for (int i = 0; i < 20; ++i) {
    fireworks.frame(24);
  }
  return coloredSpread();
}

// Steps until the first burst shows a colored spark; -1 if none within the limit.
int stepsToFirstBurst(Fireworks& fireworks, int limit) {
  for (int i = 1; i <= limit; ++i) {
    fireworks.frame(24);
    if (anyColored()) {
      return i;
    }
  }
  return -1;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_fireworks_1d_is_registered() {
  const EffectDescriptor* fireworks = EffectRegistry::global().find("fireworks-1d");
  TEST_ASSERT_NOT_NULL(fireworks);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(6, fireworks->paramCount);
  TEST_ASSERT_TRUE((fireworks->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((fireworks->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_EQUAL_INT(kGravity, ledframe::findParam(*fireworks, "gravity"));
  TEST_ASSERT_EQUAL_INT(kSide, ledframe::findParam(*fireworks, "side"));
  TEST_ASSERT_EQUAL_INT(kColor, ledframe::findParam(*fireworks, "color"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*fireworks, "palette"));
}

void test_frame_without_a_step_is_not_drawn() {
  Fireworks fireworks;
  TEST_ASSERT_FALSE(fireworks.frame(0));   // no time passed
  TEST_ASSERT_FALSE(fireworks.frame(20));  // less than one step (24 ms)
  TEST_ASSERT_TRUE(fireworks.frame(10));   // 30 ms in total: one step
  TEST_ASSERT_TRUE(litPixels() > 0);       // the rocket
}

void test_same_seed_gives_the_same_fireworks() {
  Fireworks first;
  first.run(5000, 20);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Fireworks second;
  second.run(5000, 20);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_fireworks_do_not_depend_on_fps() {
  // Steps run at a fixed rate, so the same time gives the same steps with the same
  // random numbers at any frame rate.
  Fireworks fast;
  fast.run(5000, 10);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  Fireworks slow;
  slow.run(5000, 40);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_rocket_rises_from_the_bottom_and_bursts() {
  Fireworks fireworks;
  fireworks.frame(24);
  // The rocket starts in the bottom row, in a column away from the edges.
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 0; x < canvas.width(); ++x) {
      if (lit(canvas.at(x, y))) {
        TEST_ASSERT_TRUE(y >= 8);
        TEST_ASSERT_TRUE(x >= 1 && x <= 12);
        TEST_ASSERT_TRUE(!colored(canvas.at(x, y)));  // white
      }
    }
  }
  // Within 20 s the rockets climb into the upper half and burst into colored sparks.
  bool high = false;
  bool burst = false;
  for (int i = 0; i < 20000 / 24; ++i) {
    fireworks.frame(24);
    for (int x = 0; x < canvas.width(); ++x) {
      high = high || lit(canvas.at(x, 4));
    }
    burst = burst || anyColored();
  }
  TEST_ASSERT_TRUE(high);
  TEST_ASSERT_TRUE(burst);
}

void test_heavier_gravity_bursts_sooner() {
  // The same seed gives the same peak height; with more gravity the rocket gets there
  // faster.
  Fireworks light;
  light.params[kGravity] = 0;
  const int lightSteps = stepsToFirstBurst(light, 3000);
  canvas.clear();
  Fireworks heavy;
  heavy.params[kGravity] = 255;
  const int heavySteps = stepsToFirstBurst(heavy, 3000);
  printf("steps to the first burst: gravity 0: %d, gravity 255: %d\n", lightSteps, heavySteps);
  TEST_ASSERT_TRUE(lightSteps > 0 && heavySteps > 0);
  TEST_ASSERT_TRUE(heavySteps < lightSteps);
}

void test_bursts_spread_evenly() {
  // The sparks fly out evenly in all directions, so a burst spreads about as far across
  // as along. WLED's sparks fly out mostly sideways: 8.0 across and 5.65 along for the
  // first seed.
  for (uint16_t seed : {1234, 7, 999}) {
    const Spread spread = firstBurst(seed);
    printf("seed %u: spread across %.2f, along %.2f\n", static_cast<unsigned>(seed), spread.x,
           spread.y);
    TEST_ASSERT_TRUE(spread.x > 1.0f);
    TEST_ASSERT_TRUE(spread.x / spread.y > 0.8f && spread.x / spread.y < 1.25f);
  }
}

void test_bursts_have_random_strength() {
  float smallest = 1000.0f;
  float largest = 0.0f;
  for (uint16_t seed : {1, 2, 3, 4, 5, 6, 7, 8}) {
    const Spread spread = firstBurst(seed);
    smallest = std::min(smallest, spread.x);
    largest = std::max(largest, spread.x);
  }
  printf("burst spread across: smallest %.2f, largest %.2f\n", smallest, largest);
  TEST_ASSERT_TRUE(largest > 1.4f * smallest);
}

void test_one_color_mode_uses_the_color() {
  // Red sparks and a white rocket: blending to white, dimming and reddening keep green
  // equal to blue.
  Fireworks fireworks;
  fireworks.params[kColors] = 1;  // color
  fireworks.params[kColor] = 0xFF0000;
  bool red = false;
  for (int i = 0; i < 20000 / 24; ++i) {
    fireworks.frame(24);
    for (int p = 0; p < canvas.size(); ++p) {
      TEST_ASSERT_EQUAL_UINT8(canvas[p].g, canvas[p].b);
      red = red || canvas[p].r > canvas[p].g + 100;
    }
  }
  TEST_ASSERT_TRUE(red);
}

void test_colors_come_from_the_palette() {
  // Every heat color and white have red >= green >= blue. Blending to white, dimming,
  // reddening and fading keep that order, so no pixel may break it.
  Fireworks fireworks;
  fireworks.params[kPalette] = Palettes::indexOf("heat");
  for (int i = 0; i < 20000 / 24; ++i) {
    fireworks.frame(24);
    for (int p = 0; p < canvas.size(); ++p) {
      TEST_ASSERT_TRUE(canvas[p].r >= canvas[p].g);
      TEST_ASSERT_TRUE(canvas[p].g >= canvas[p].b);
    }
  }
}

void test_strip_fires_from_the_chosen_end() {
  // 30 x 1: side 0 always starts at the beginning, side 255 (nearly) always at the end.
  TEST_ASSERT_TRUE(canvas.resize(30, 1));
  Fireworks start;
  start.params[kSide] = 0;
  start.frame(24);
  TEST_ASSERT_TRUE(lit(canvas.at(0, 0)));
  TEST_ASSERT_FALSE(lit(canvas.at(29, 0)));
  canvas.clear();
  Fireworks end;
  end.params[kSide] = 255;
  end.frame(24);
  TEST_ASSERT_TRUE(lit(canvas.at(29, 0)));
  TEST_ASSERT_FALSE(lit(canvas.at(0, 0)));
  // 1 x 30: from the bottom, or from the top with side 255.
  TEST_ASSERT_TRUE(canvas.resize(1, 30));
  Fireworks up;
  up.params[kSide] = 0;
  up.frame(24);
  TEST_ASSERT_TRUE(lit(canvas.at(0, 29)));
  TEST_ASSERT_FALSE(lit(canvas.at(0, 0)));
}

void test_rocket_and_sparks_glide_without_jumps() {
  // The rocket and the sparks move in fractions of a pixel and are soft dots, so a
  // pixel they reach brightens over steps. Put into whole pixels, as in WLED, they
  // light a new pixel from dark to full at once. Pixels only darken by the fade, so
  // rises of a pixel per step above half the light show the jumps.
  Fireworks fireworks;
  std::vector<uint32_t> before = pixels();
  int jumps = 0;
  for (int i = 0; i < 20000 / 24; ++i) {
    fireworks.frame(24);
    const std::vector<uint32_t> after = pixels();
    for (size_t p = 0; p < after.size(); ++p) {
      jumps += brightness(after[p]) - brightness(before[p]) > 128 ? 1 : 0;
    }
    before = after;
  }
  printf("rises of a pixel per step above 128 in 20 s: %d\n", jumps);
  TEST_ASSERT_TRUE(jumps < 80);  // 35 here (bursts start with a white flash); 553 drawn
                                 // into whole pixels
}

void test_extreme_sizes_and_values_work() {
  const struct {
    uint16_t width, height;
  } sizes[] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {30, 1}, {1, 30}, {32, 32}, {1024, 1}};
  for (const auto& size : sizes) {
    for (int32_t gravity : {0, 255}) {
      TEST_ASSERT_TRUE(canvas.resize(size.width, size.height));
      Fireworks fireworks;
      fireworks.params[kGravity] = gravity;
      fireworks.params[kSize] = 20;
      fireworks.run(20000, 40);
      fireworks.frame(24);
    }
  }
  TEST_ASSERT_TRUE(canvas.resize(32, 32));
  Fireworks big;
  big.run(20000, 24);
  TEST_ASSERT_TRUE(litPixels() >= 0);
}

void test_long_stall_draws_once() {
  Fireworks fireworks;
  TEST_ASSERT_TRUE(fireworks.frame(60000));  // a minute without frames: no hang
  TEST_ASSERT_TRUE(fireworks.frame(24));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fireworks_1d_is_registered);
  RUN_TEST(test_frame_without_a_step_is_not_drawn);
  RUN_TEST(test_same_seed_gives_the_same_fireworks);
  RUN_TEST(test_fireworks_do_not_depend_on_fps);
  RUN_TEST(test_rocket_rises_from_the_bottom_and_bursts);
  RUN_TEST(test_heavier_gravity_bursts_sooner);
  RUN_TEST(test_bursts_spread_evenly);
  RUN_TEST(test_bursts_have_random_strength);
  RUN_TEST(test_one_color_mode_uses_the_color);
  RUN_TEST(test_colors_come_from_the_palette);
  RUN_TEST(test_strip_fires_from_the_chosen_end);
  RUN_TEST(test_rocket_and_sparks_glide_without_jumps);
  RUN_TEST(test_extreme_sizes_and_values_work);
  RUN_TEST(test_long_stall_draws_once);
  return UNITY_END();
}

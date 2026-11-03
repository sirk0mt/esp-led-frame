#include <lib8tion.h>
#include <unity.h>

#include <vector>

#include "effects/EffectRegistry.h"
#include "effects/PaintImage.h"
#include "render/Palettes.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::Palettes;
using ledframe::ParamKind;

namespace {

Canvas canvas;
Palettes palettes;

uint32_t packed(const CRGB& color) {
  return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(3, 2));
}
void tearDown() {}

void test_builtin_effects_register_without_issues() {
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
}

void test_solid_descriptor() {
  const EffectDescriptor* solid = EffectRegistry::global().find("solid");
  TEST_ASSERT_NOT_NULL(solid);
  TEST_ASSERT_EQUAL_STRING("Solid", solid->name);
  TEST_ASSERT_TRUE((solid->flags & ledframe::kFlagStatic) != 0);
  TEST_ASSERT_FALSE((solid->flags & ledframe::kFlagHidden) != 0);
  TEST_ASSERT_EQUAL_UINT8(1, solid->paramCount);
  TEST_ASSERT_EQUAL_STRING("color", solid->params[0].id);
  TEST_ASSERT_TRUE(solid->params[0].kind == ParamKind::Color);
  TEST_ASSERT_EQUAL_INT(0, ledframe::findParam(*solid, "color"));
}

void test_solid_fills_canvas_with_color() {
  const EffectDescriptor* solid = EffectRegistry::global().find("solid");
  int32_t params[ledframe::kMaxParams];
  ledframe::loadDefaults(*solid, params);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, static_cast<uint32_t>(params[0]));

  params[0] = 0x123456;
  FrameContext ctx{canvas, 0, 0, params, palettes.get(0)};
  ledframe::EffectSlot slot;
  Effect* effect = slot.create(*solid);
  TEST_ASSERT_NOT_NULL(effect);
  effect->begin(ctx);
  TEST_ASSERT_TRUE(effect->render(ctx));
  for (int i = 0; i < canvas.size(); ++i) {
    TEST_ASSERT_EQUAL_HEX32(0x123456, packed(canvas[i]));
  }
}

// With gamma on, the LEDs get the corrected color, while the stored parameter stays
// the color chosen on the screen.
void test_solid_with_gamma_correction() {
  const EffectDescriptor* solid = EffectRegistry::global().find("solid");
  int32_t params[ledframe::kMaxParams];
  ledframe::loadDefaults(*solid, params);
  params[0] = 0x808080;
  FrameContext ctx{canvas, 0, 0, params, palettes.get(0), true};
  ledframe::EffectSlot slot;
  Effect* effect = slot.create(*solid);
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0x383838, packed(canvas[0]));  // 128 -> 56
  TEST_ASSERT_EQUAL_HEX32(0x808080, static_cast<uint32_t>(params[0]));

  params[0] = 0xFFC0CB;  // pink
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0xFF899A, packed(canvas[0]));
  ctx.gamma = false;
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0xFFC0CB, packed(canvas[0]));
}

void test_status_descriptor() {
  const EffectDescriptor* status = EffectRegistry::global().find("status");
  TEST_ASSERT_NOT_NULL(status);
  TEST_ASSERT_TRUE((status->flags & ledframe::kFlagHidden) != 0);
  TEST_ASSERT_TRUE((status->flags & ledframe::kFlagStatic) != 0);
  TEST_ASSERT_EQUAL_UINT8(0, status->paramCount);
}

// Renders the status effect on a w x h canvas and returns the lit pixels as text
// rows, '#' for red and '.' for black.
void renderStatus(uint16_t width, uint16_t height, char (*rows)[16]) {
  TEST_ASSERT_TRUE(canvas.resize(width, height));
  canvas.fill(CRGB(0, 0, 255));  // The effect must clear old content.
  const EffectDescriptor* status = EffectRegistry::global().find("status");
  FrameContext ctx{canvas, 0, 0, nullptr, palettes.get(0)};
  ledframe::EffectSlot slot;
  Effect* effect = slot.create(*status);
  effect->begin(ctx);
  TEST_ASSERT_TRUE(effect->render(ctx));
  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t x = 0; x < width; ++x) {
      const uint32_t color = packed(canvas.at(x, y));
      TEST_ASSERT_TRUE(color == 0xFF0000 || color == 0);
      rows[y][x] = color == 0xFF0000 ? '#' : '.';
    }
    rows[y][width] = '\0';
  }
}

void test_status_draws_triangle_on_15x10() {
  char rows[10][16];
  renderStatus(15, 10, rows);
  TEST_ASSERT_EQUAL_STRING(".......#.......", rows[0]);
  TEST_ASSERT_EQUAL_STRING("......###......", rows[2]);
  TEST_ASSERT_EQUAL_STRING("###############", rows[9]);
  // Every row is symmetric and at least as wide as the row above.
  int previous = 0;
  for (int y = 0; y < 10; ++y) {
    int lit = 0;
    for (int x = 0; x < 15; ++x) {
      TEST_ASSERT_EQUAL_CHAR(rows[y][x], rows[y][14 - x]);
      lit += rows[y][x] == '#' ? 1 : 0;
    }
    TEST_ASSERT_TRUE(lit >= previous);
    previous = lit;
  }
}

void test_status_draws_triangle_on_even_width() {
  char rows[4][16];
  renderStatus(4, 4, rows);
  TEST_ASSERT_EQUAL_STRING(".##.", rows[0]);
  TEST_ASSERT_EQUAL_STRING("####", rows[3]);
  renderStatus(1, 1, rows);
  TEST_ASSERT_EQUAL_STRING("#", rows[0]);
}

// --- Rainbow --------------------------------------------------------------------

namespace {

enum RainbowParam : uint8_t { kSpeed, kAngle, kDensity, kPalette };

// One rainbow instance with its own parameter values on the shared canvas.
struct Rainbow {
  explicit Rainbow(int32_t angle = 0, int32_t speed = 64, int32_t density = 10,
                   int32_t palette = 0) {
    descriptor = EffectRegistry::global().find("rainbow");
    ledframe::loadDefaults(*descriptor, params);
    params[kAngle] = angle;
    params[kSpeed] = speed;
    params[kDensity] = density;
    params[kPalette] = palette;
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs) {
    return FrameContext{canvas, nowMs, 0, params, palettes.get(params[kPalette])};
  }
  bool render(uint32_t nowMs) {
    FrameContext ctx = context(nowMs);
    return effect->render(ctx);
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
};

std::vector<uint32_t> pixels() {
  std::vector<uint32_t> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back(packed(canvas[i]));
  }
  return out;
}

bool columnsUniform() {
  for (int x = 0; x < canvas.width(); ++x) {
    for (int y = 1; y < canvas.height(); ++y) {
      if (packed(canvas.at(x, y)) != packed(canvas.at(x, 0))) {
        return false;
      }
    }
  }
  return true;
}

bool rowsUniform() {
  for (int y = 0; y < canvas.height(); ++y) {
    for (int x = 1; x < canvas.width(); ++x) {
      if (packed(canvas.at(x, y)) != packed(canvas.at(0, y))) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

void test_rainbow_descriptor() {
  const EffectDescriptor* rainbow = EffectRegistry::global().find("rainbow");
  TEST_ASSERT_NOT_NULL(rainbow);
  TEST_ASSERT_TRUE((rainbow->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_TRUE((rainbow->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_FALSE((rainbow->flags & ledframe::kFlagStatic) != 0);
  TEST_ASSERT_EQUAL_UINT8(4, rainbow->paramCount);
  TEST_ASSERT_EQUAL_INT(kSpeed, ledframe::findParam(*rainbow, "speed"));
  TEST_ASSERT_EQUAL_INT(kAngle, ledframe::findParam(*rainbow, "angle"));
  TEST_ASSERT_EQUAL_INT(kDensity, ledframe::findParam(*rainbow, "density"));
  TEST_ASSERT_EQUAL_INT(kPalette, ledframe::findParam(*rainbow, "palette"));
  TEST_ASSERT_TRUE(rainbow->params[kPalette].kind == ParamKind::Palette);
}

void test_rainbow_is_deterministic_for_a_fixed_time() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow first(30);
  TEST_ASSERT_TRUE(first.render(12345));
  const std::vector<uint32_t> a = pixels();
  canvas.clear();
  TEST_ASSERT_TRUE(first.render(12345));
  TEST_ASSERT_TRUE(a == pixels());  // Same instance, same time.
  canvas.clear();
  Rainbow second(30);
  TEST_ASSERT_TRUE(second.render(12345));
  TEST_ASSERT_TRUE(a == pixels());  // New instance, same time.
}

void test_rainbow_angle_0_gives_uniform_columns() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(0);
  rainbow.render(777);
  TEST_ASSERT_TRUE(columnsUniform());
  TEST_ASSERT_FALSE(rowsUniform());  // Columns differ from each other.
  TEST_ASSERT_TRUE(packed(canvas.at(0, 0)) != packed(canvas.at(7, 0)));
}

void test_rainbow_angle_90_gives_uniform_rows() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(90);
  rainbow.render(777);
  TEST_ASSERT_TRUE(rowsUniform());
  TEST_ASSERT_FALSE(columnsUniform());
}

void test_rainbow_moves_with_time() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(0, 64);
  rainbow.render(0);
  const std::vector<uint32_t> start = pixels();
  rainbow.render(1000);  // 64 palette steps later
  TEST_ASSERT_FALSE(start == pixels());
  // Phase is periodic: speed 64 repeats the whole palette every 4 s.
  rainbow.render(4000);
  TEST_ASSERT_TRUE(start == pixels());
  // Long uptimes do not overflow the phase: same picture 2^32 / 1000 s later.
  rainbow.render(4000000000u);
  TEST_ASSERT_TRUE(start == pixels());
}

void test_rainbow_speed_0_is_still() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(0, 0);
  TEST_ASSERT_TRUE(rainbow.render(0));  // First frame is drawn.
  const std::vector<uint32_t> first = pixels();
  TEST_ASSERT_FALSE(rainbow.render(5000));  // Nothing changes: no new frame.
  TEST_ASSERT_TRUE(first == pixels());
  rainbow.change(kDensity, 20);  // A parameter change draws once more.
  TEST_ASSERT_TRUE(rainbow.render(6000));
  TEST_ASSERT_FALSE(rainbow.render(7000));
}

void test_rainbow_reacts_to_angle_change() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(0);
  rainbow.render(100);
  TEST_ASSERT_TRUE(columnsUniform());
  rainbow.change(kAngle, 90);
  rainbow.render(100);
  TEST_ASSERT_TRUE(rowsUniform());
  rainbow.change(kAngle, 180);
  rainbow.render(100);
  TEST_ASSERT_TRUE(columnsUniform());
}

void test_rainbow_uses_the_palette() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Rainbow rainbow(0, 64, 10, Palettes::indexOf("rainbow"));
  rainbow.render(500);
  const std::vector<uint32_t> rainbowColors = pixels();
  Rainbow heat(0, 64, 10, Palettes::indexOf("heat"));
  heat.render(500);
  TEST_ASSERT_FALSE(rainbowColors == pixels());
}

// --- Galaxy ---------------------------------------------------------------------

namespace {

enum GalaxyParam : uint8_t { kGalaxyDensity, kGalaxyFade, kGalaxyColors, kGalaxyPalette };

constexpr uint16_t kSeed = 1234;

// One galaxy instance with a fixed random seed. Frames advance by stepMs, so the
// same time span can be rendered at different frame rates.
struct Galaxy {
  Galaxy(int32_t density, int32_t fade, int32_t colors = 0, int32_t palette = 0) {
    descriptor = EffectRegistry::global().find("galaxy");
    ledframe::loadDefaults(*descriptor, params);
    params[kGalaxyDensity] = density;
    params[kGalaxyFade] = fade;
    params[kGalaxyColors] = colors;
    params[kGalaxyPalette] = palette;
    random16_set_seed(kSeed);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t deltaMs) {
    return FrameContext{canvas, nowMs, deltaMs, params, palettes.get(params[kGalaxyPalette])};
  }
  // Renders frames of stepMs until totalMs have passed.
  void run(uint32_t totalMs, uint32_t stepMs) {
    for (uint32_t elapsed = 0; elapsed < totalMs; elapsed += stepMs) {
      nowMs += stepMs;
      FrameContext ctx = context(stepMs);
      effect->render(ctx);
    }
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
  uint32_t nowMs = 0;
};

int litPixels() {
  int lit = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    lit += packed(canvas[i]) != 0 ? 1 : 0;
  }
  return lit;
}

uint8_t maxRed() {
  uint8_t red = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    red = canvas[i].r > red ? canvas[i].r : red;
  }
  return red;
}

std::vector<bool> litMask() {
  std::vector<bool> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back(packed(canvas[i]) != 0);
  }
  return out;
}

}  // namespace

void test_galaxy_descriptor() {
  const EffectDescriptor* galaxy = EffectRegistry::global().find("galaxy");
  TEST_ASSERT_NOT_NULL(galaxy);
  TEST_ASSERT_TRUE((galaxy->flags & ledframe::kFlagPalette) != 0);
  TEST_ASSERT_FALSE((galaxy->flags & ledframe::kFlagStatic) != 0);
  TEST_ASSERT_EQUAL_UINT8(4, galaxy->paramCount);
  TEST_ASSERT_EQUAL_INT(kGalaxyDensity, ledframe::findParam(*galaxy, "density"));
  TEST_ASSERT_EQUAL_INT(kGalaxyFade, ledframe::findParam(*galaxy, "fade"));
  TEST_ASSERT_EQUAL_INT(kGalaxyColors, ledframe::findParam(*galaxy, "colors"));
  TEST_ASSERT_EQUAL_INT(kGalaxyPalette, ledframe::findParam(*galaxy, "palette"));
  const ledframe::ParamSpec& colors = galaxy->params[kGalaxyColors];
  TEST_ASSERT_TRUE(colors.kind == ParamKind::Select);
  TEST_ASSERT_EQUAL_STRING("random|palette", colors.options);
  TEST_ASSERT_EQUAL_UINT8(2, ledframe::selectOptionCount(colors));
}

void test_galaxy_lit_pixels_match_density() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // On average 20 sparks per second with a long fade: after 1 s about 20 pixels
  // are lit (the count is random, standard deviation about 4.5), and none has
  // faded out yet.
  Galaxy galaxy(20, 10000);
  galaxy.run(1000, 20);
  const int lit = litPixels();
  TEST_ASSERT_TRUE(lit >= 8);
  TEST_ASSERT_TRUE(lit <= 32);
  galaxy.run(1000, 20);
  TEST_ASSERT_TRUE(litPixels() > lit);
  TEST_ASSERT_TRUE(litPixels() <= 64);
}

void test_galaxy_steady_state_follows_fade() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // A short fade keeps only the last few sparks lit. A spark lives
  // max(r, g, b) / 255 * fade, on average 3/4 of 200 ms: about 50 * 0.15 = 7.5.
  Galaxy galaxy(50, 200);
  galaxy.run(5000, 20);
  const int lit = litPixels();
  TEST_ASSERT_TRUE(lit >= 2);
  TEST_ASSERT_TRUE(lit <= 16);
}

void test_galaxy_spark_timing_is_irregular() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  canvas.clear();
  // 50 sparks per second at 20 ms frames is one spark per frame on average. With
  // random intervals some frames have none and some have several (Poisson: about
  // 37% and 26%), unlike a fixed rhythm of exactly one per frame. Fading only lowers
  // channels, so a pixel with a higher channel than before is a new spark.
  Galaxy galaxy(50, 300);
  std::vector<uint32_t> previous = pixels();
  int framesWithout = 0;
  int framesWithSeveral = 0;
  for (int frame = 0; frame < 500; ++frame) {
    galaxy.run(20, 20);
    int sparks = 0;
    for (int i = 0; i < canvas.size(); ++i) {
      const uint32_t was = previous[i];
      const CRGB& now = canvas[i];
      sparks += (now.r > ((was >> 16) & 0xFF) || now.g > ((was >> 8) & 0xFF) || now.b > (was & 0xFF))
                    ? 1
                    : 0;
    }
    framesWithout += sparks == 0 ? 1 : 0;
    framesWithSeveral += sparks >= 2 ? 1 : 0;
    previous = pixels();
  }
  TEST_ASSERT_TRUE(framesWithout > 100);
  TEST_ASSERT_TRUE(framesWithSeveral > 75);
}

void test_galaxy_sparks_differ_in_brightness() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  canvas.clear();
  // Random R, G and B as in v1: sparks are not all at full brightness.
  Galaxy galaxy(100, 10000);
  galaxy.run(300, 20);
  int dim = 0;
  int bright = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    const CRGB& c = canvas[i];
    const uint8_t top = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    dim += top > 0 && top < 160 ? 1 : 0;
    bright += top > 220 ? 1 : 0;
  }
  TEST_ASSERT_TRUE(dim > 0);
  TEST_ASSERT_TRUE(bright > 0);
}

void test_galaxy_is_deterministic_for_a_fixed_seed() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  Galaxy first(30, 1500);
  first.run(2000, 20);
  const std::vector<uint32_t> a = pixels();
  canvas.clear();  // The engine clears the canvas before creating an effect.
  Galaxy second(30, 1500);
  second.run(2000, 20);
  TEST_ASSERT_TRUE(a == pixels());
}

void test_galaxy_spark_count_does_not_depend_on_fps() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  canvas.clear();
  Galaxy fast(25, 10000);
  fast.run(1000, 10);
  const std::vector<bool> fastMask = litMask();
  canvas.clear();
  Galaxy slow(25, 10000);
  slow.run(1000, 100);
  // Same seed and the same spark times: the same pixels are lit.
  TEST_ASSERT_TRUE(fastMask == litMask());
}

// Pixels that are still the faded white of the starting canvas (r == g == b > 0).
int grayPixels(uint8_t level) {
  int count = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    count += packed(canvas[i]) == packed(CRGB(level, level, level)) ? 1 : 0;
  }
  return count;
}

void test_galaxy_fade_does_not_depend_on_fps() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // Linear fade: half of a 1000 ms fade takes 127 of 255 at any frame rate. Density
  // 1 adds at most a spark or two, which may cover a pixel of the white canvas.
  const uint32_t steps[2] = {10, 50};
  for (uint32_t step : steps) {
    Galaxy galaxy(1, 1000);
    canvas.fill(CRGB::White);
    galaxy.run(500, step);
    TEST_ASSERT_TRUE(grayPixels(128) >= canvas.size() - 3);
  }
  // After the whole fade time the white is gone.
  Galaxy galaxy(1, 1000);
  canvas.fill(CRGB::White);
  galaxy.run(1000, 10);
  for (int level = 1; level < 256; ++level) {
    TEST_ASSERT_EQUAL_INT(0, grayPixels(static_cast<uint8_t>(level)));
  }
  TEST_ASSERT_TRUE(litPixels() <= 5);
}

void test_galaxy_weak_channels_fade_first() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // As in v1 every channel loses the same amount, so a spark changes its color while
  // it fades: orange (255, 100, 0) becomes pure red after 100/255 of the fade.
  Galaxy galaxy(1, 1000);
  canvas.clear();
  canvas[0] = CRGB(255, 100, 0);
  galaxy.run(400, 20);  // 102 of 255 taken
  TEST_ASSERT_EQUAL_HEX32(packed(CRGB(153, 0, 0)), packed(canvas[0]));
}

void test_galaxy_palette_colors() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  canvas.clear();
  // The ocean palette has no strong red, random colors do. 200 ms at 100 sparks
  // per second gives about 20 sparks that are still almost at full brightness.
  Galaxy ocean(100, 10000, 1, Palettes::indexOf("ocean"));
  ocean.run(200, 20);
  TEST_ASSERT_TRUE(litPixels() > 0);
  TEST_ASSERT_TRUE(maxRed() < 160);
  canvas.clear();
  Galaxy hues(100, 10000, 0);
  hues.run(200, 20);
  TEST_ASSERT_TRUE(maxRed() > 200);
}

// --- Color flow -----------------------------------------------------------------

namespace {

enum ColorFlowParam : uint8_t { kFlowSpeed, kFlowMaxStep };

// One color-flow instance with a fixed random seed, rendered frame by frame.
struct ColorFlow {
  ColorFlow(int32_t speed, int32_t maxStep) {
    descriptor = EffectRegistry::global().find("color-flow");
    ledframe::loadDefaults(*descriptor, params);
    params[kFlowSpeed] = speed;
    params[kFlowMaxStep] = maxStep;
    random16_set_seed(kSeed);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t deltaMs) {
    return FrameContext{canvas, nowMs, deltaMs, params, palettes.get(0)};
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

bool canvasUniform() {
  for (int i = 1; i < canvas.size(); ++i) {
    if (packed(canvas[i]) != packed(canvas[0])) {
      return false;
    }
  }
  return true;
}

}  // namespace

void test_color_flow_descriptor() {
  const EffectDescriptor* flow = EffectRegistry::global().find("color-flow");
  TEST_ASSERT_NOT_NULL(flow);
  TEST_ASSERT_EQUAL_STRING("Color flow", flow->name);
  TEST_ASSERT_EQUAL_UINT8(0, flow->flags);
  TEST_ASSERT_EQUAL_UINT8(2, flow->paramCount);
  TEST_ASSERT_EQUAL_INT(kFlowSpeed, ledframe::findParam(*flow, "speed"));
  TEST_ASSERT_EQUAL_INT(kFlowMaxStep, ledframe::findParam(*flow, "max_step"));
}

void test_color_flow_channels_stay_in_range_and_bounce() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // 50 steps per second of at most 10 units, 20 ms frames: at most 10 units per frame.
  constexpr int kMaxMove = 10;
  ColorFlow flow(50, 10);
  TEST_ASSERT_TRUE(flow.frame(0));
  int previous[3] = {canvas[0].r, canvas[0].g, canvas[0].b};
  int direction[3] = {1, 1, 1};  // All channels start rising.
  int bounces[3][2] = {};        // [channel][0 = at 0, 1 = at 255]
  for (int i = 0; i < 3000; ++i) {
    flow.frame(20);
    TEST_ASSERT_TRUE(canvasUniform());
    const int current[3] = {canvas[0].r, canvas[0].g, canvas[0].b};
    for (int c = 0; c < 3; ++c) {
      const int diff = current[c] - previous[c];
      // No wrap-around: a channel never jumps across the range.
      TEST_ASSERT_TRUE(diff >= -kMaxMove && diff <= kMaxMove);
      if (diff != 0 && (diff > 0 ? 1 : -1) != direction[c]) {
        // The direction changes only at a bound.
        TEST_ASSERT_TRUE(previous[c] == 0 || previous[c] == 255);
        ++bounces[c][previous[c] == 255 ? 1 : 0];
        direction[c] = -direction[c];
      }
      previous[c] = current[c];
    }
  }
  for (int c = 0; c < 3; ++c) {
    TEST_ASSERT_TRUE(bounces[c][0] > 5);
    TEST_ASSERT_TRUE(bounces[c][1] > 5);
  }
}

void test_color_flow_does_not_depend_on_fps() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  // Slow enough that no channel reaches a bound in 500 ms: the color after the
  // same time is the same at 10 ms and at 100 ms frames.
  ColorFlow fast(10, 2);
  fast.run(500, 10);
  const uint32_t fastColor = packed(canvas[0]);
  ColorFlow slow(10, 2);
  slow.run(500, 100);
  TEST_ASSERT_EQUAL_HEX32(fastColor, packed(canvas[0]));
  ColorFlow start(10, 2);
  start.frame(0);
  TEST_ASSERT_TRUE(fastColor != packed(canvas[0]));  // It did move.
}

void test_color_flow_speed_0_is_still() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  ColorFlow flow(0, 10);
  TEST_ASSERT_TRUE(flow.frame(0));  // First frame is drawn.
  const uint32_t first = packed(canvas[0]);
  TEST_ASSERT_FALSE(flow.frame(5000));
  TEST_ASSERT_EQUAL_HEX32(first, packed(canvas[0]));
}

// --- Paint ----------------------------------------------------------------------

using ledframe::PaintImage;
using ledframe::PaintOp;

namespace {

PaintOp pixel(uint16_t x, uint16_t y, uint32_t color) {
  PaintOp op;
  op.kind = PaintOp::kPixel;
  op.x = x;
  op.y = y;
  op.color = CRGB(color);
  return op;
}

PaintOp fill(uint32_t color) {
  PaintOp op;
  op.kind = PaintOp::kFill;
  op.color = CRGB(color);
  return op;
}

}  // namespace

void test_paint_descriptor() {
  const EffectDescriptor* paint = EffectRegistry::global().find("paint");
  TEST_ASSERT_NOT_NULL(paint);
  TEST_ASSERT_EQUAL_STRING("Paint", paint->name);
  TEST_ASSERT_EQUAL_STRING("paint", paint->widget);
  TEST_ASSERT_TRUE((paint->flags & ledframe::kFlagStatic) != 0);
  TEST_ASSERT_TRUE((paint->flags & ledframe::kFlag2D) != 0);
  TEST_ASSERT_EQUAL_UINT8(0, paint->paramCount);
}

void test_paint_image_pixels_fill_and_clear() {
  PaintImage image;
  TEST_ASSERT_TRUE(image.resize(4, 3));
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(3, 2)));
  const uint32_t start = image.version();
  TEST_ASSERT_TRUE(image.apply(pixel(3, 2, 0x112233)));
  TEST_ASSERT_EQUAL_HEX32(0x112233, packed(image.at(3, 2)));
  TEST_ASSERT_EQUAL_HEX32(0x112233, packed(image.data()[2 * 4 + 3]));
  TEST_ASSERT_TRUE(image.version() > start);
  TEST_ASSERT_TRUE(image.apply(fill(0x0000FF)));
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, packed(image.at(0, 0)));
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, packed(image.at(3, 2)));
  PaintOp clear;
  clear.kind = PaintOp::kClear;
  TEST_ASSERT_TRUE(image.apply(clear));
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(3, 2)));
}

void test_paint_image_rejects_pixels_outside() {
  PaintImage image;
  TEST_ASSERT_TRUE(image.resize(4, 3));
  const uint32_t start = image.version();
  TEST_ASSERT_FALSE(image.apply(pixel(4, 0, 0xFFFFFF)));
  TEST_ASSERT_FALSE(image.apply(pixel(0, 3, 0xFFFFFF)));
  TEST_ASSERT_FALSE(image.apply(pixel(0xFFFF, 0xFFFF, 0xFFFFFF)));
  TEST_ASSERT_EQUAL_UINT32(start, image.version());  // Nothing changed.
  for (int i = 0; i < image.size(); ++i) {
    TEST_ASSERT_EQUAL_HEX32(0, packed(image.data()[i]));
  }
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(-1, 0)));
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(4, 0)));
}

void test_paint_image_resize_keeps_the_common_part() {
  PaintImage image;
  TEST_ASSERT_TRUE(image.resize(3, 2));
  for (uint16_t y = 0; y < 2; ++y) {
    for (uint16_t x = 0; x < 3; ++x) {
      image.apply(pixel(x, y, 0x010000u * (y + 1) + x + 1));  // unique colors
    }
  }
  // Wider and taller: old pixels stay at their x, y; new pixels are black.
  TEST_ASSERT_TRUE(image.resize(5, 4));
  for (uint16_t y = 0; y < 4; ++y) {
    for (uint16_t x = 0; x < 5; ++x) {
      const uint32_t expected = (x < 3 && y < 2) ? 0x010000u * (y + 1) + x + 1 : 0;
      TEST_ASSERT_EQUAL_HEX32(expected, packed(image.at(x, y)));
    }
  }
  // Narrower and shorter: the top-left part stays.
  TEST_ASSERT_TRUE(image.resize(2, 1));
  TEST_ASSERT_EQUAL_HEX32(0x010001, packed(image.at(0, 0)));
  TEST_ASSERT_EQUAL_HEX32(0x010002, packed(image.at(1, 0)));
  // Back to larger: the dropped part does not come back.
  TEST_ASSERT_TRUE(image.resize(3, 2));
  TEST_ASSERT_EQUAL_HEX32(0x010002, packed(image.at(1, 0)));
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(2, 0)));
  TEST_ASSERT_EQUAL_HEX32(0, packed(image.at(0, 1)));
  // Invalid sizes keep the picture.
  TEST_ASSERT_FALSE(image.resize(0, 5));
  TEST_ASSERT_FALSE(image.resize(ledframe::kMaxLeds, 2));
  TEST_ASSERT_EQUAL_UINT16(3, image.width());
  TEST_ASSERT_EQUAL_UINT16(2, image.height());
}

void test_paint_image_round_trip() {
  PaintImage image;
  TEST_ASSERT_TRUE(image.resize(15, 10));
  image.apply(fill(0x102030));
  image.apply(pixel(0, 0, 0xFF0000));
  image.apply(pixel(14, 9, 0x00FF00));
  image.apply(pixel(7, 4, 0x0000FF));
  static uint8_t buffer[PaintImage::kMaxBytes];
  const size_t length = image.serialize(buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT32(8 + 15 * 10 * 3, length);
  TEST_ASSERT_EQUAL_UINT8('L', buffer[0]);
  TEST_ASSERT_EQUAL_UINT8('P', buffer[1]);
  TEST_ASSERT_EQUAL_UINT8(1, buffer[2]);
  TEST_ASSERT_EQUAL_UINT8(15, buffer[4]);  // width, little-endian
  TEST_ASSERT_EQUAL_UINT8(10, buffer[6]);  // height
  TEST_ASSERT_EQUAL_UINT8(0xFF, buffer[8]);  // first pixel red

  PaintImage copy;
  TEST_ASSERT_TRUE(copy.deserialize(buffer, length));
  TEST_ASSERT_EQUAL_UINT16(15, copy.width());
  TEST_ASSERT_EQUAL_UINT16(10, copy.height());
  for (int i = 0; i < image.size(); ++i) {
    TEST_ASSERT_EQUAL_HEX32(packed(image.data()[i]), packed(copy.data()[i]));
  }
  TEST_ASSERT_EQUAL_UINT32(0, image.serialize(buffer, length - 1));  // too small
}

void test_paint_image_rejects_broken_data() {
  PaintImage image;
  TEST_ASSERT_TRUE(image.resize(2, 2));
  image.apply(fill(0xABCDEF));
  static uint8_t buffer[PaintImage::kMaxBytes];
  const size_t length = image.serialize(buffer, sizeof(buffer));

  PaintImage target;
  TEST_ASSERT_TRUE(target.resize(3, 1));
  target.apply(fill(0x111111));
  TEST_ASSERT_FALSE(target.deserialize(buffer, length - 1));  // short
  TEST_ASSERT_FALSE(target.deserialize(buffer, 4));           // header cut
  TEST_ASSERT_FALSE(target.deserialize(nullptr, length));
  buffer[0] = 'X';
  TEST_ASSERT_FALSE(target.deserialize(buffer, length));  // magic
  buffer[0] = 'L';
  buffer[2] = 2;
  TEST_ASSERT_FALSE(target.deserialize(buffer, length));  // format version
  buffer[2] = 1;
  buffer[4] = 0;
  TEST_ASSERT_FALSE(target.deserialize(buffer, length));  // width 0
  buffer[4] = 2;
  // The target keeps its picture after every failure.
  TEST_ASSERT_EQUAL_UINT16(3, target.width());
  TEST_ASSERT_EQUAL_HEX32(0x111111, packed(target.at(2, 0)));
  TEST_ASSERT_TRUE(target.deserialize(buffer, length));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, packed(target.at(1, 1)));
}

void test_paint_effect_shows_the_shared_image() {
  PaintImage& image = ledframe::sharedPaintImage();
  TEST_ASSERT_TRUE(image.resize(15, 10));
  image.apply(fill(0));
  image.apply(pixel(2, 1, 0x00FF00));
  image.apply(pixel(14, 9, 0xFF00FF));

  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  canvas.fill(CRGB(1, 2, 3));
  const EffectDescriptor* paint = EffectRegistry::global().find("paint");
  FrameContext ctx{canvas, 0, 0, nullptr, palettes.get(0)};
  ledframe::EffectSlot slot;
  Effect* effect = slot.create(*paint);
  effect->begin(ctx);
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, packed(canvas.at(2, 1)));
  TEST_ASSERT_EQUAL_HEX32(0xFF00FF, packed(canvas.at(14, 9)));
  TEST_ASSERT_EQUAL_HEX32(0, packed(canvas.at(0, 0)));

  // A canvas of another size gets the common part and black elsewhere.
  TEST_ASSERT_TRUE(canvas.resize(20, 5));
  canvas.fill(CRGB(1, 2, 3));
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, packed(canvas.at(2, 1)));
  TEST_ASSERT_EQUAL_HEX32(0, packed(canvas.at(19, 4)));

  // With gamma on, painted pixels are corrected on the canvas, not in the picture.
  image.apply(pixel(2, 1, 0x808080));
  ctx.gamma = true;
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_HEX32(0x383838, packed(canvas.at(2, 1)));
  TEST_ASSERT_EQUAL_HEX32(0x808080, packed(image.at(2, 1)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builtin_effects_register_without_issues);
  RUN_TEST(test_solid_descriptor);
  RUN_TEST(test_solid_fills_canvas_with_color);
  RUN_TEST(test_solid_with_gamma_correction);
  RUN_TEST(test_status_descriptor);
  RUN_TEST(test_status_draws_triangle_on_15x10);
  RUN_TEST(test_status_draws_triangle_on_even_width);
  RUN_TEST(test_rainbow_descriptor);
  RUN_TEST(test_rainbow_is_deterministic_for_a_fixed_time);
  RUN_TEST(test_rainbow_angle_0_gives_uniform_columns);
  RUN_TEST(test_rainbow_angle_90_gives_uniform_rows);
  RUN_TEST(test_rainbow_moves_with_time);
  RUN_TEST(test_rainbow_speed_0_is_still);
  RUN_TEST(test_rainbow_reacts_to_angle_change);
  RUN_TEST(test_rainbow_uses_the_palette);
  RUN_TEST(test_galaxy_descriptor);
  RUN_TEST(test_galaxy_lit_pixels_match_density);
  RUN_TEST(test_galaxy_steady_state_follows_fade);
  RUN_TEST(test_galaxy_spark_timing_is_irregular);
  RUN_TEST(test_galaxy_sparks_differ_in_brightness);
  RUN_TEST(test_galaxy_is_deterministic_for_a_fixed_seed);
  RUN_TEST(test_galaxy_spark_count_does_not_depend_on_fps);
  RUN_TEST(test_galaxy_fade_does_not_depend_on_fps);
  RUN_TEST(test_galaxy_weak_channels_fade_first);
  RUN_TEST(test_galaxy_palette_colors);
  RUN_TEST(test_color_flow_descriptor);
  RUN_TEST(test_color_flow_channels_stay_in_range_and_bounce);
  RUN_TEST(test_color_flow_does_not_depend_on_fps);
  RUN_TEST(test_color_flow_speed_0_is_still);
  RUN_TEST(test_paint_descriptor);
  RUN_TEST(test_paint_image_pixels_fill_and_clear);
  RUN_TEST(test_paint_image_rejects_pixels_outside);
  RUN_TEST(test_paint_image_resize_keeps_the_common_part);
  RUN_TEST(test_paint_image_round_trip);
  RUN_TEST(test_paint_image_rejects_broken_data);
  RUN_TEST(test_paint_effect_shows_the_shared_image);
  return UNITY_END();
}

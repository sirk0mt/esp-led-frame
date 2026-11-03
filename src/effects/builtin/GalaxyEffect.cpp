// Sparks in random pixels that fade out over time (v1 "Galaxy").
//
// The look follows v1: sparks come at irregular moments, have random colors and
// brightness, and every channel fades by the same amount, so bright sparks live
// longer and a spark changes its color while its weaker channels go dark. Unlike
// v1 everything is measured in time, so the look does not depend on FPS:
//   - the time to the next spark is random (exponential, mean 1000 / density ms),
//     which gives clusters and gaps like v1's workers with random delays
//   - fading subtracts 255 * deltaMs / fade from every channel, with the remainder
//     carried over, so a channel at 255 goes dark after exactly `fade` ms
//   density  average number of new sparks per second
//   fade     time in ms for a full channel (255) to go dark
//   colors   random: random R, G and B as in v1; palette: random palette color at
//            random brightness
//   palette  palette for colors = palette
// The effect has its own xorshift32 generator. begin() seeds it from FastLED
// random16(), which App seeds at start and tests set with random16_set_seed().
#include <lib8tion.h>

#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kDensity, kFade, kColors, kPalette };
enum Colors : int32_t { kColorsRandom, kColorsPalette };

constexpr ParamSpec kParams[] = {
    {"density", "Density", ParamKind::Slider, 1, 255, 20},
    {"fade", "Fade", ParamKind::Slider, 100, 10000, 1500},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsRandom, "random|palette"},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

// Limits deltaMs, so a long stall cannot overflow the fade sum.
constexpr uint32_t kMaxStepMs = 10000;
// Darkest palette spark; darker ones would be hard to see at all.
constexpr uint8_t kMinPaletteBrightness = 48;

class GalaxyEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    rng_ = (static_cast<uint32_t>(random16()) << 16) | random16();
    if (rng_ == 0) {
      rng_ = 1;  // xorshift never leaves 0.
    }
    fadeCarry_ = 0;
    untilSparkMs_ = nextInterval(ctx.params[kDensity]);
  }

  void onParamChanged(uint8_t index, FrameContext& ctx) override {
    if (index == kDensity) {
      // Otherwise a change from 1 to 255 waits for the old, long interval.
      untilSparkMs_ = nextInterval(ctx.params[kDensity]);
    } else if (index == kFade) {
      fadeCarry_ = 0;
    }
  }

  bool render(FrameContext& ctx) override {
    Canvas& canvas = ctx.canvas;
    const uint32_t deltaMs = ctx.deltaMs < kMaxStepMs ? ctx.deltaMs : kMaxStepMs;
    const bool faded = fade(canvas, deltaMs, static_cast<uint32_t>(ctx.params[kFade]));

    const int32_t density = ctx.params[kDensity];
    const bool fromPalette = ctx.params[kColors] == kColorsPalette;
    const uint16_t size = canvas.size();
    CRGB* pixels = canvas.data();
    uint16_t sparks = 0;
    untilSparkMs_ -= static_cast<float>(deltaMs);
    while (untilSparkMs_ <= 0.0f) {
      if (sparks == size) {
        untilSparkMs_ = nextInterval(density);  // More would only overwrite each other.
        break;
      }
      pixels[nextBelow(size)] = fromPalette ? paletteColor(ctx.palette) : randomColor();
      ++sparks;
      untilSparkMs_ += nextInterval(density);
    }
    return faded || sparks > 0;
  }

 private:
  // Subtracts the same amount from every channel (v1 dimming, but per time).
  bool fade(Canvas& canvas, uint32_t deltaMs, uint32_t fadeMs) {
    fadeCarry_ += deltaMs * 255u;
    uint32_t amount = fadeCarry_ / fadeMs;
    fadeCarry_ %= fadeMs;
    if (amount == 0) {
      return false;
    }
    const uint8_t step = static_cast<uint8_t>(amount > 255u ? 255u : amount);
    CRGB* pixels = canvas.data();
    const uint16_t size = canvas.size();
    for (uint16_t i = 0; i < size; ++i) {
      pixels[i].r = qsub8(pixels[i].r, step);
      pixels[i].g = qsub8(pixels[i].g, step);
      pixels[i].b = qsub8(pixels[i].b, step);
    }
    return true;
  }

  CRGB randomColor() {
    const uint32_t bits = next();
    return CRGB(static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8),
                static_cast<uint8_t>(bits >> 16));
  }

  CRGB paletteColor(const CRGBPalette16& palette) {
    const uint32_t bits = next();
    const uint8_t brightness = static_cast<uint8_t>(
        kMinPaletteBrightness + (((bits >> 8) & 0xFFu) * (256u - kMinPaletteBrightness) >> 8));
    return ColorFromPalette(palette, static_cast<uint8_t>(bits), brightness, LINEARBLEND);
  }

  // Exponential interval with mean 1000 / density ms: sparks as a Poisson process.
  float nextInterval(int32_t density) {
    // 24 random bits give u in (0, 1]; -ln(u) is never infinite.
    const float u = static_cast<float>((next() >> 8) + 1u) * (1.0f / 16777216.0f);
    return -std::log(u) * 1000.0f / static_cast<float>(density);
  }

  uint16_t nextBelow(uint16_t limit) {
    return static_cast<uint16_t>((static_cast<uint64_t>(next()) * limit) >> 32);
  }

  uint32_t next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  uint32_t rng_ = 1;
  uint32_t fadeCarry_ = 0;
  float untilSparkMs_ = 0.0f;
};

}  // namespace

REGISTER_EFFECT(GalaxyEffect, "galaxy", "Galaxy", 60, ledframe::kFlagPalette, kParams);

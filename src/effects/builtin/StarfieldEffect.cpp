// Starfield: stars fly out of the center, faster and brighter on the way (warp).
//
// A star is born near the center with a random direction and moves out along it.
// Its speed grows with its distance, so it creeps out of the middle and rushes past
// the edge, as if it came closer. One simulation step (every kStepMs) moves every
// star and starts new ones at random; steps run at a fixed rate counted from
// deltaMs, so the stars follow the same paths at any FPS.
//
// Positions are fixed point with 8 fraction bits (1/256 pixel). A star lights the
// four pixels around its position, each by its share of the area (antialiasing), so
// slow stars glide between pixels instead of jumping. The canvas fades a little
// every frame and a star never darkens a pixel, which leaves short trails. A new
// star lights up gradually instead of popping up: in 0.3 s at full speed and up to
// 2 s for slow stars, which would otherwise stay a still dot that suddenly appears.
//
// Smooth for the eye, not only in numbers: the eye sees LED light about as the
// square root of its PWM level. The shares of the four pixels and the fade-in are
// therefore computed on the seen light, and each pixel gets its square as the PWM
// level. Otherwise a star half way between two pixels would look brighter than on
// one pixel, and it would seem to jump from pixel to pixel. Levels are computed in
// 16 bits and rounded up or down at random with the right odds (temporal dithering),
// so the darkest steps (0, 1, 2 of 255) do not jump; the dither pattern comes from
// the time and the pixel, so the same time still gives the same picture.
//   speed    how fast the stars fly out
//   density  new stars per second on average
//   colors   color: every star in the color parameter; random: a random hue per star
//   color    star color for colors = color (default: white with a touch of blue)
// Randomness comes from FastLED random8() and random16(); App seeds them at start.
#include <lib8tion.h>

#include "effects/EffectRegistry.h"
// After the line above: colorpalettes.h needs the FastLED color types (CHSV).
#include <colorpalettes.h>

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kDensity, kColors, kColor };
enum Colors : int32_t { kColorsColor, kColorsRandom };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 1, 100, 30},
    {"density", "Density", ParamKind::Slider, 1, 100, 20},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsColor, "color|random"},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xD0E0FF},
};

constexpr uint32_t kStepMs = 10;
constexpr uint8_t kMaxStepsPerFrame = 20;  // after a stall the stars catch up only a little
constexpr uint8_t kMaxStars = 64;
constexpr uint32_t kTrailMs = 120;  // a trail pixel fades out in about this time
// Seen light of a star at the center, out of 256 at the edge: PWM level 64 of 255.
constexpr uint32_t kMinLight = 128;
// Steps (10 ms) for a new star to reach its full light: kFadeInSpeedSteps / speed,
// within kMinFadeInSteps..kMaxFadeInSteps.
constexpr uint32_t kFadeInSpeedSteps = 3000;
constexpr uint32_t kMinFadeInSteps = 30;
constexpr uint32_t kMaxFadeInSteps = 200;

// Distance in 1/65536 pixel per step at speed 1: 1/30 pixel per second near the center.
constexpr uint32_t kBaseStep = 22;
// Growth of the distance per step at speed 1, out of 65536: 5% per second.
constexpr uint32_t kGrowth = 33;

// 32-bit integer hash (lowbias32, as in twinkle).
uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

struct Star {
  uint32_t distance;  // from the center, 1/65536 pixel
  uint16_t angle;     // direction, 65536 = one turn
  uint16_t age;       // steps since birth, stops at 65535
  uint8_t hue;        // rainbow hue for colors = random
  bool alive;
};

class StarfieldEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    const Canvas& canvas = ctx.canvas;
    // Center between the middle pixels, in 1/256 pixel.
    centerX_ = (canvas.width() - 1) * 128;
    centerY_ = (canvas.height() - 1) * 128;
    // A star is gone once it is past the farthest corner: 3/4 of the longer side plus
    // 2 pixels is more than the half diagonal plus one pixel.
    const uint32_t longer =
        static_cast<uint32_t>(canvas.width() > canvas.height() ? canvas.width() : canvas.height());
    maxDistance_ = (longer * 3u / 4u + 2u) << 16;
    // Full brightness at the nearest edge, at least a pixel out.
    const uint32_t shorter =
        static_cast<uint32_t>(canvas.width() < canvas.height() ? canvas.width() : canvas.height());
    brightDistance_ = (shorter > 2u ? shorter / 2u : 1u) << 16;
    for (Star& star : stars_) {
      star.alive = false;
    }
    carryMs_ = 0;
  }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    const uint8_t density = static_cast<uint8_t>(ctx.params[kDensity]);
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      if (steps < kMaxStepsPerFrame) {
        step(speed, density);
        ++steps;
      }
    }

    Canvas& canvas = ctx.canvas;
    const uint32_t fade = ctx.deltaMs * 255u / kTrailMs;
    canvas.fadeToBlackBy(static_cast<uint8_t>(fade > 255u ? 255u : fade));
    const bool randomColors = ctx.params[kColors] == kColorsRandom;
    const CRGB color = ctx.userColor(ctx.params[kColor]);
    const uint32_t fadeIn = kFadeInSpeedSteps / speed;
    const uint32_t fadeInSteps = fadeIn < kMinFadeInSteps   ? kMinFadeInSteps
                                 : fadeIn > kMaxFadeInSteps ? kMaxFadeInSteps
                                                            : fadeIn;
    // Dither noise of this frame: from the time, so the same time gives the same picture.
    const uint32_t frameNoise = hash(ctx.nowMs * 0x27D4EB2Du);
    for (const Star& star : stars_) {
      if (star.alive) {
        // Rainbow palette colors are made for LEDs: no user color correction.
        draw(canvas, star,
             randomColors ? ColorFromPalette(RainbowColors_p, star.hue, 255, LINEARBLEND) : color,
             fadeInSteps, frameNoise);
      }
    }
    return true;
  }

 private:
  void step(uint32_t speed, uint8_t density) {
    for (Star& star : stars_) {
      if (star.alive) {
        // 64-bit: distance * growth needs up to 45 bits.
        star.distance +=
            speed * kBaseStep +
            static_cast<uint32_t>(static_cast<uint64_t>(star.distance) * speed * kGrowth >> 16);
        star.alive = star.distance < maxDistance_;
        star.age = star.age < 0xFFFFu ? static_cast<uint16_t>(star.age + 1) : star.age;
      }
    }
    // density stars per second = density / 100 per 10 ms step.
    if (random8(100) < density) {
      for (Star& star : stars_) {
        if (!star.alive) {
          star.distance = static_cast<uint32_t>(random8()) << 7;  // up to half a pixel out
          star.angle = random16();
          star.age = 0;
          // Drawn in both color modes, so switching them keeps the same star paths.
          star.hue = random8();
          star.alive = true;
          break;  // All slots taken: this star is skipped.
        }
      }
    }
  }

  void draw(Canvas& canvas, const Star& star, const CRGB& color, uint32_t fadeInSteps,
            uint32_t frameNoise) const {
    // Position in 1/256 pixel. cos16 and sin16 give -32767..32767.
    const int32_t radius = static_cast<int32_t>(star.distance >> 8);
    const int32_t x =
        centerX_ + static_cast<int32_t>(static_cast<int64_t>(radius) * cos16(star.angle) / 32768);
    const int32_t y =
        centerY_ + static_cast<int32_t>(static_cast<int64_t>(radius) * sin16(star.angle) / 32768);
    if (x < -256 || y < -256) {
      return;  // Fully off the canvas; also keeps the shifts below on non-negative values.
    }

    // Seen light 0..256: brighter on the way out (kMinLight at the center, full at the
    // nearest edge), and a new star lights up gradually.
    const uint32_t out = star.distance < brightDistance_ ? star.distance : brightDistance_;
    const uint32_t full = kMinLight + (256u - kMinLight) * (out >> 8) / (brightDistance_ >> 8);
    const uint32_t age = star.age < fadeInSteps ? star.age : fadeInSteps;
    const uint32_t seen = full * age / fadeInSteps;
    if (seen == 0) {
      return;
    }

    // Share of the star in each of the four pixels around its position, 0..256 per axis.
    const int32_t px = (x + 256) >> 8;  // +256: x may be just below 0
    const int32_t py = (y + 256) >> 8;
    const uint32_t fx = static_cast<uint32_t>(x & 0xFF);
    const uint32_t fy = static_cast<uint32_t>(y & 0xFF);
    light(canvas, px - 1, py - 1, color, seen * (256u - fx) * (256u - fy) >> 16, frameNoise);
    light(canvas, px, py - 1, color, seen * fx * (256u - fy) >> 16, frameNoise);
    light(canvas, px - 1, py, color, seen * (256u - fx) * fy >> 16, frameNoise);
    light(canvas, px, py, color, seen * fx * fy >> 16, frameNoise);
  }

  // Lights a pixel to at least the color at seen light 0..256 (PWM level: its square),
  // dithered to 8 bits; overlapping light never darkens it.
  static void light(Canvas& canvas, int32_t x, int32_t y, const CRGB& color, uint32_t seen,
                    uint32_t frameNoise) {
    if (seen == 0 || x < 0 || y < 0 || x >= canvas.width() || y >= canvas.height()) {
      return;
    }
    const uint32_t level = seen * seen;  // PWM level, 0..65536
    const uint32_t index = static_cast<uint32_t>(y * canvas.width() + x);
    const uint32_t noise = hash(frameNoise ^ (index * 0x9E3779B9u)) & 0xFFu;
    auto channel = [level, noise](uint8_t c) {
      const uint32_t value = ((c * level) >> 8) + noise;  // 8.8 fixed point
      return static_cast<uint8_t>(value >> 8 > 255u ? 255u : value >> 8);
    };
    CRGB& pixel = canvas.at(x, y);
    pixel.r = brighter(pixel.r, channel(color.r));
    pixel.g = brighter(pixel.g, channel(color.g));
    pixel.b = brighter(pixel.b, channel(color.b));
  }

  static uint8_t brighter(uint8_t a, uint8_t b) { return a > b ? a : b; }

  int32_t centerX_ = 0;
  int32_t centerY_ = 0;
  uint32_t maxDistance_ = 0;     // 1/65536 pixel
  uint32_t brightDistance_ = 0;  // 1/65536 pixel
  uint32_t carryMs_ = 0;
  Star stars_[kMaxStars] = {};
};

}  // namespace

REGISTER_EFFECT(StarfieldEffect, "starfield", "Starfield", 67, ledframe::kFlag2D, kParams);

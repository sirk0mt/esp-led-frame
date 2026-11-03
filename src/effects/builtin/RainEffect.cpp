// Rain: drops falling down the columns with a fading trail (digital rain).
//
// Simulation steps run at a fixed rate counted from deltaMs: every step a drop falls
// one row, and empty columns start new drops at random. The picture is drawn every
// frame from the drop positions in fractions of a row (the time since the last step
// over the step length), so even slow drops glide: the head lights up the next row
// gradually while the rows behind it dim smoothly. The same time gives the same
// picture at any FPS.
//
// Seen from a pixel at d rows behind the head (d < 0: still ahead of it):
//   -1 .. 0  lights up from black to full, half way to white (the arriving head)
//    0 .. 1  full; the white part fades out (the head turns into the drop color)
//    1 ..    the drop color, losing a quarter of its light per row (the trail)
// Smooth for the eye, not only in numbers:
//   - The eye sees LED light about as the square root of its PWM level, so a linear
//     PWM ramp looks like a pixel that pops up and then waits. The ramps run on the
//     seen light, and the PWM level is its square. At whole rows the levels are the
//     same as with a plain PWM ramp (1/4 less per row).
//   - Levels are computed in 16 bits and rounded up or down at random with the right
//     odds (temporal dithering), so the darkest steps (0, 1, 2 of 255) do not jump.
//     The dither pattern comes from the time and the pixel, so the same time still
//     gives the same picture.
// A column is free for a new drop once the head of its drop has left the bottom;
// the trail of the old drop keeps fading out below the matrix edge meanwhile.
//
// With colors = random every drop has its own rainbow hue: a hash of its column and
// the step it started in.
//   speed    rows per second a drop falls
//   density  chance in % that an empty column starts a drop in one step
//   colors   color: every drop in the color parameter; random: a random hue per drop
//   color    drop color for colors = color (default: green)
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
    {"speed", "Speed", ParamKind::Slider, 1, 60, 12},
    {"density", "Density", ParamKind::Slider, 1, 50, 10},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsColor, "color|random"},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0x00FF40},
};

constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall the rain catches up only a little
// Drops on screen at once. A 15 x 10 matrix needs about 3 per column at most (one
// falling, older trails fading out below); on wider layouts new drops wait for a slot.
constexpr uint8_t kMaxDrops = 64;
// Seen trail light 0..256 at k whole rows behind the drop color pixel:
// 256 * sqrt(3/4)^k, so the PWM level (its square) loses 1/4 per row. It ends with
// the first level that would stay below a quarter of the darkest PWM step.
constexpr uint16_t kTrail[] = {256, 222, 192, 166, 144, 125, 108, 94, 81, 70, 61, 53, 46,
                               39,  34,  30,  26,  22,  19,  17,  14, 12, 11, 9,  0};
constexpr int32_t kTrailRows = sizeof(kTrail) / sizeof(kTrail[0]) - 1;

// 32-bit integer hash (lowbias32, as in twinkle).
uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Pixel d/256 rows behind the head (see the table above): the drop color moved
// towards white by white/256, then scaled by the PWM level (light/256)^2, dithered
// to 8 bits with noise 0..255.
CRGB shade(const CRGB& color, int32_t d, uint8_t noise) {
  uint32_t light = 256;
  uint32_t white = 0;
  if (d < 0) {
    light = static_cast<uint32_t>(d + 256);  // arriving head
    white = 128;
  } else if (d < 256) {
    white = static_cast<uint32_t>(256 - d) / 2;  // head turning into the drop color
  } else {
    const int32_t row = (d - 256) >> 8;
    if (row >= kTrailRows) {
      return CRGB(0, 0, 0);  // past the end of the trail
    }
    const uint32_t frac = static_cast<uint32_t>(d - 256) & 0xFFu;
    light = (kTrail[row] * (256u - frac) + kTrail[row + 1] * frac) >> 8;
  }
  const uint32_t level = light * light;  // PWM level, 0..65536
  auto channel = [level, white, noise](uint8_t c) {
    const uint32_t lit = c + (((255u - c) * white) >> 8);
    const uint32_t value = ((lit * level) >> 8) + noise;  // 8.8 fixed point
    return static_cast<uint8_t>(value >> 8 > 255u ? 255u : value >> 8);
  };
  return CRGB(channel(color.r), channel(color.g), channel(color.b));
}

struct Drop {
  uint32_t started;  // step count when the drop started, one row above the matrix
  uint16_t column;
  bool alive;
};

class RainEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    width_ = ctx.canvas.width();
    height_ = ctx.canvas.height();
    for (Drop& drop : drops_) {
      drop.alive = false;
    }
    for (uint8_t& bits : busy_) {
      bits = 0;
    }
    carryMs_ = 0;
    stepCount_ = 0;
    drawn_ = true;  // the first frame clears the canvas
    seed_ = (static_cast<uint32_t>(random16()) << 16) | random16();
  }

  bool render(FrameContext& ctx) override {
    const uint32_t stepMs = 1000u / static_cast<uint32_t>(ctx.params[kSpeed]);
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= stepMs) {
      carryMs_ -= stepMs;
      if (steps < kMaxStepsPerFrame) {
        step(static_cast<uint8_t>(ctx.params[kDensity]));
        ++steps;
      }
    }

    bool any = false;
    for (const Drop& drop : drops_) {
      any = any || drop.alive;
    }
    if (!any && !drawn_) {
      return false;  // No drops, and the canvas is already dark.
    }
    drawn_ = any;

    Canvas& canvas = ctx.canvas;
    canvas.clear();
    const bool randomColors = ctx.params[kColors] == kColorsRandom;
    const CRGB color = ctx.userColor(ctx.params[kColor]);
    // How far the drops are on their way to the next row, 0..255.
    const int32_t phase = static_cast<int32_t>(carryMs_ * 256u / stepMs);
    // Dither noise of this frame: from the time, so the same time gives the same picture.
    const uint32_t frameNoise = hash(seed_ ^ (ctx.nowMs * 0x27D4EB2Du));
    for (const Drop& drop : drops_) {
      if (drop.alive) {
        draw(canvas, drop, phase, randomColors ? dropColor(drop) : color, frameNoise);
      }
    }
    return true;
  }

 private:
  void step(uint8_t density) {
    ++stepCount_;
    for (Drop& drop : drops_) {
      if (!drop.alive) {
        continue;
      }
      const int32_t head = headRow(drop);
      if (head == height_) {
        setBusy(drop.column, false);  // The head left: the column may start a new drop.
      }
      drop.alive = head - kTrailRows - 1 < height_;
    }
    for (int x = 0; x < width_; ++x) {
      if (!isBusy(x) && random8(100) < density) {
        start(static_cast<uint16_t>(x));
      }
    }
  }

  void start(uint16_t column) {
    for (Drop& drop : drops_) {
      if (!drop.alive) {
        drop.started = stepCount_;
        drop.column = column;
        drop.alive = true;
        setBusy(column, true);
        return;
      }
    }
    // All slots taken: no drop this time.
  }

  // Whole row of the head at the last step; -1 is just above the matrix.
  int32_t headRow(const Drop& drop) const {
    return static_cast<int32_t>(stepCount_ - drop.started) - 1;
  }

  void draw(Canvas& canvas, const Drop& drop, int32_t phase, const CRGB& color,
            uint32_t frameNoise) const {
    const int32_t head = headRow(drop) * 256 + phase;  // in 1/256 row
    const int32_t first = (head >> 8) - kTrailRows - 1;
    const int32_t last = (head >> 8) + 1;
    for (int32_t y = first < 0 ? 0 : first; y <= last && y < height_; ++y) {
      const int32_t d = head - y * 256;
      if (d <= -256) {
        continue;
      }
      const uint32_t index = static_cast<uint32_t>(y * width_ + drop.column);
      const CRGB lit =
          shade(color, d, static_cast<uint8_t>(hash(frameNoise ^ (index * 0x9E3779B9u))));
      // An older trail below a new head: the brighter light wins.
      CRGB& pixel = canvas.at(drop.column, y);
      pixel.r = lit.r > pixel.r ? lit.r : pixel.r;
      pixel.g = lit.g > pixel.g ? lit.g : pixel.g;
      pixel.b = lit.b > pixel.b ? lit.b : pixel.b;
    }
  }

  // Random hue of a drop from its column and start step. Rainbow palette colors are
  // made for LEDs, so they need no user color correction.
  CRGB dropColor(const Drop& drop) const {
    const uint8_t hue =
        static_cast<uint8_t>(hash(seed_ ^ (static_cast<uint32_t>(drop.column) * 0x9E3779B9u) ^
                                  (drop.started * 0x85EBCA6Bu)) >>
                             24);
    return ColorFromPalette(RainbowColors_p, hue, 255, LINEARBLEND);
  }

  bool isBusy(int x) const { return (busy_[x >> 3] >> (x & 7)) & 1u; }
  void setBusy(int x, bool on) {
    const uint8_t bit = static_cast<uint8_t>(1u << (x & 7));
    busy_[x >> 3] = static_cast<uint8_t>(on ? busy_[x >> 3] | bit : busy_[x >> 3] & ~bit);
  }

  int width_ = 0;
  int height_ = 0;
  uint32_t carryMs_ = 0;
  uint32_t stepCount_ = 0;
  uint32_t seed_ = 0;
  bool drawn_ = false;  // the last frame had drops
  Drop drops_[kMaxDrops] = {};
  uint8_t busy_[(ledframe::kMaxLeds + 7) / 8] = {};  // a bit per column with a falling drop
};

}  // namespace

REGISTER_EFFECT(RainEffect, "rain", "Rain", 72, ledframe::kFlag2D, kParams);

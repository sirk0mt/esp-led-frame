// Metaballs: soft blobs that drift around, merge when they meet and part again.
//
// A few balls move on Lissajous curves: x and y follow sines of different speeds,
// so the paths cross the whole matrix without repeating soon. Every pixel adds up
// the field of all balls, R^2 / d^2 (d: distance to the ball, R: ball radius), and
// the sum picks the palette color: 1 is the edge of a lone ball, near balls add up
// and melt into one blob. The far tails of all balls add up too, so a background
// level (kBackground) is taken off first: far from the balls the index is 0. The
// sines come from nowMs, not from FastLED beatsin8 (that one reads millis() itself),
// so the picture follows the effect clock.
//   speed    how fast the balls move: speed / 100 sine turns per second, times 5/8..11/8
//   count    number of balls
//   palette  blob colors from the field: low index far away, high index in a blob
//            (default: heat, dark outside and glowing inside; it needs a palette
//            that grows steadily, lava has dark bands and draws rings)
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kCount, kPalette };

constexpr int32_t kHeatPalette = 5;  // render/Palettes.cpp: ..., forest, lava, heat

constexpr uint8_t kMaxBalls = 6;

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 1, 100, 20},
    {"count", "Count", ParamKind::Slider, 1, kMaxBalls, 4},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kHeatPalette},
};

// Every ball has its own pace along x and y, in eighths of the base rate, and its own
// start phase, so no two paths are alike.
constexpr uint8_t kPaceX[kMaxBalls] = {5, 7, 9, 6, 11, 8};
constexpr uint8_t kPaceY[kMaxBalls] = {8, 6, 11, 9, 7, 10};
constexpr uint16_t kPhaseX[kMaxBalls] = {0, 21000, 43000, 11000, 54000, 32000};
constexpr uint16_t kPhaseY[kMaxBalls] = {16384, 5000, 60000, 38000, 27000, 49000};

// Field sum (in 1/256) taken off as background, and palette steps per field unit after
// it: the edge of a lone ball (field 1) gets index 120, and the index stops at 255.
constexpr uint32_t kBackground = 102;  // 0.4
constexpr uint32_t kStepsPerField = 200;

class MetaballsEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    const Canvas& canvas = ctx.canvas;
    width_ = canvas.width();
    height_ = canvas.height();
    // Ball radius: a fifth of the shorter side, at least 1 pixel, in 1/16 pixel.
    const int shorter = width_ < height_ ? width_ : height_;
    const int32_t radius = shorter > 5 ? shorter * 16 / 5 : 16;
    radius2_ = static_cast<uint32_t>(radius * radius);
  }

  bool render(FrameContext& ctx) override {
    // Ball positions in 1/16 pixel. Base rate: speed / 100 sine turns per second; the
    // angle is cut to 16 bits only after the 64-bit product, so it never jumps.
    const uint64_t base =
        static_cast<uint64_t>(ctx.nowMs) * static_cast<uint32_t>(ctx.params[kSpeed]);
    const int count = ctx.params[kCount];
    // Centers swing over 90% of the matrix around its middle.
    const int32_t middleX = (width_ - 1) * 8;
    const int32_t middleY = (height_ - 1) * 8;
    const int32_t swingX = middleX * 9 / 10;
    const int32_t swingY = middleY * 9 / 10;
    int32_t ballX[kMaxBalls];
    int32_t ballY[kMaxBalls];
    for (int b = 0; b < count; ++b) {
      // turns = nowMs * speed / 100000 * pace / 8; one turn is 65536.
      const uint16_t angleX =
          static_cast<uint16_t>(base * kPaceX[b] * 65536u / 800000u + kPhaseX[b]);
      const uint16_t angleY =
          static_cast<uint16_t>(base * kPaceY[b] * 65536u / 800000u + kPhaseY[b]);
      ballX[b] = middleX + swingX * sin16(angleX) / 32768;
      ballY[b] = middleY + swingY * sin16(angleY) / 32768;
    }

    Canvas& canvas = ctx.canvas;
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        // Field sum in 1/256: R^2 * 256 / d^2 per ball, all in 1/16 pixel units.
        uint32_t field = 0;
        for (int b = 0; b < count; ++b) {
          const int32_t dx = x * 16 - ballX[b];
          const int32_t dy = y * 16 - ballY[b];
          // + 16: a ball right on a pixel center gives a large field, not a division by 0.
          const uint32_t d2 = static_cast<uint32_t>(dx * dx + dy * dy) + 16u;
          field += radius2_ * 256u / d2;
        }
        // NOWRAP: the index is a strength, not a hue. LINEARBLEND would blend the top
        // indexes (241..255) back towards the first palette entry.
        const uint32_t index =
            field > kBackground ? (field - kBackground) * kStepsPerField / 256u : 0u;
        pixels[y * width_ + x] =
            ColorFromPalette(ctx.palette, static_cast<uint8_t>(index > 255u ? 255u : index), 255,
                             LINEARBLEND_NOWRAP);
      }
    }
    return true;
  }

 private:
  int width_ = 0;
  int height_ = 0;
  uint32_t radius2_ = 0;  // ball radius squared, in 1/256 pixel^2
};

}  // namespace

REGISTER_EFFECT(MetaballsEffect, "metaballs", "Metaballs", 54,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

// Drift: a spiral of colored points winding and unwinding around the center.
//
// Adapted from "Drift" in WLED (wled00/FX.cpp, mode_2DDrift; by Stepko,
// https://editor.soulmatelights.com/gallery/884-drift, modified by Andrew Tuline).
// Written again for this firmware; the motion, the colors and the options follow WLED.
//
// Points sit every quarter pixel from radius 1 to half the longer side of the matrix,
// one arm from the center outwards. Every point turns around the center at its own
// rate, faster near the center: the point at radius i turns by t * (max - i) degrees,
// t = time / (32 - speed) ms. The arm therefore winds up into a spiral, falls apart
// into rings and points, and winds up again. Each point takes its color from the
// palette by its radius, and the colors move outwards with time. One simulation step
// (one frame in WLED, about 42 per second) halves the light of the whole matrix (short
// trails), draws the points and optionally blurs the matrix. Steps run at a fixed rate
// counted from deltaMs, so the effect looks the same at any FPS.
//
// Unlike WLED, which puts every point into a whole pixel, the points are kept in
// fractions of a pixel and drawn as soft dots (render/SoftDraw.h), so on a small matrix
// they glide instead of jumping. The angles are exact: WLED computes them in float from
// the ever growing time, which loses precision after a few hours of running. The arm
// turns around the middle of the matrix; WLED's center (width / 2 + width % 2) is a
// pixel off the middle on an odd side. A change of speed carries on from the current
// angles instead of jumping.
//   speed  rotation speed: time runs at 1 / (32 - speed) of real time (WLED speed / 8)
//   blur   blur strength per step (WLED intensity / 8; 0 = no blur)
//   twin   on: a second arm, mirrored across the diagonal
//   smear  on: the blur keeps every pixel and is twice as strong (brighter, smeared)
//   size   point radius, in tenths of a pixel
//   palette  point colors
#include <lib8tion.h>

#include "effects/EffectRegistry.h"
#include "render/SoftDraw.h"

namespace {

using ledframe::Canvas;
using ledframe::drawSoftDot;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kBlur, kTwin, kSmear, kSize, kPalette };

constexpr ParamSpec kParams[] = {
    {"speed", "Rotation speed", ParamKind::Slider, 0, 31, 16},
    {"blur", "Blur", ParamKind::Slider, 0, 31, 0},
    {"twin", "Twin", ParamKind::Toggle, 0, 1, 0},
    {"smear", "Smear", ParamKind::Toggle, 0, 1, 0},
    {"size", "Size", ParamKind::Slider, 10, 20, 10},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kStepMs = 24;          // about 42 steps per second, the WLED frame rate
constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall the points skip ahead, drawn once
constexpr uint8_t kFade = 128;            // trail fade per step, out of 256
constexpr uint32_t kTimeOne = 65536;      // the effect time t is kept in 1/65536 units
// t wraps at a multiple of both of its periods: 1440 (a full turn of every point, in
// t * quarter pixels / 360) and 5120 (the palette, t / 20 over 256 entries).
constexpr uint64_t kTimeWrap = 46080ull * kTimeOne;

class DriftEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override {
    time_ = 0;
    carryMs_ = 0;
  }

  bool render(FrameContext& ctx) override {
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      // WLED: t = now / (32 - speed). The step adds its share, so a new speed carries on
      // from the current angles.
      const uint32_t divisor = 32u - static_cast<uint32_t>(ctx.params[kSpeed]);
      time_ = (time_ + kStepMs * kTimeOne / divisor) % kTimeWrap;
      if (steps < kMaxStepsPerFrame) {
        step(ctx);
        ++steps;
      }
    }
    return steps > 0;  // no step: the same picture as in the last frame
  }

 private:
  void step(FrameContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    const float radius = static_cast<float>(ctx.params[kSize]) / 10.0f;
    const bool twin = ctx.params[kTwin] != 0;

    canvas.fadeToBlackBy(kFade);

    const float centerX = static_cast<float>(width - 1) / 2.0f;
    const float centerY = static_cast<float>(height - 1) / 2.0f;
    // WLED: i from 1 while below max (half the longer side), in quarters. Here in
    // quarter pixels: quarter = 4 * i, turns = 4 * (max - i).
    const int maxQuarters = 4 * ((width > height ? width : height) / 2);
    const uint8_t paletteShift = static_cast<uint8_t>(time_ / (20u * kTimeOne));
    for (int quarter = 4; quarter < maxQuarters; ++quarter) {
      // Angle t * (max - i) degrees, in 1/65536 turns: t * turns / 1440.
      const uint64_t turns = static_cast<uint64_t>(maxQuarters - quarter);
      const uint16_t angle = static_cast<uint16_t>(time_ * turns / 1440u);
      const float distance = static_cast<float>(quarter) / 4.0f;
      const float sine = static_cast<float>(sin16(angle)) / 32767.0f * distance;
      const float cosine = static_cast<float>(cos16(angle)) / 32767.0f * distance;
      // WLED: palette index i * 20 + t / 20.
      const CRGB color = ColorFromPalette(
          ctx.palette, static_cast<uint8_t>(quarter * 5 + paletteShift), 255, LINEARBLEND);
      drawSoftDot(canvas, centerX + sine, centerY + cosine, color, radius);
      if (twin) {
        drawSoftDot(canvas, centerX + cosine, centerY + sine, color, radius);
      }
    }

    const bool smear = ctx.params[kSmear] != 0;
    const int blur = ctx.params[kBlur];
    canvas.blur(static_cast<uint8_t>(smear ? blur * 2 : blur), smear);
  }

  uint64_t time_ = 0;  // effect time t in 1/65536 units, wrapped at kTimeWrap
  uint32_t carryMs_ = 0;
};

}  // namespace

REGISTER_EFFECT(DriftEffect, "drift", "Drift", 61, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

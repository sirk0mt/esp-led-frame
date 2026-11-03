// Swirl: a palette spiral turning around the center.
//
// Every pixel takes the palette color at its angle around the center, plus its
// distance times the twist, shifted by a phase that grows with time:
//   index = angle + distance * twist -/+ nowMs * speed
// Angle and distance are computed once in begin() (atan2 and sqrt) and kept as one
// byte each: the angle in 1/256 turns, the distance in 1/255 of the distance to the
// farthest corner. The twist therefore gives the same spiral on any matrix size.
// render() uses only integer math, as in radial-rainbow.
//   speed      palette steps per second (256 steps = one full turn); 0 = still
//   twist      quarter turns of the spiral from the center to the farthest corner
//   direction  clockwise or counterclockwise turning (the matrix y axis points down)
//   palette    spiral colors
#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kTwist, kDirection, kPalette };
enum Direction : int32_t { kClockwise, kCounterclockwise };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 48},
    {"twist", "Twist", ParamKind::Slider, 0, 32, 6},
    {"direction", "Direction", ParamKind::Select, 0, 0, kClockwise, "clockwise|counterclockwise"},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr float kPi = 3.14159265f;

class SwirlEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    const Canvas& canvas = ctx.canvas;
    const float centerX = (canvas.width() - 1) / 2.0f;
    const float centerY = (canvas.height() - 1) / 2.0f;
    const float farthest = std::sqrt(centerX * centerX + centerY * centerY);
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        const float dx = x - centerX;
        const float dy = y - centerY;
        // atan2 gives -pi..pi; 0 points right, and with y down the angle grows clockwise.
        const float turns = std::atan2(dy, dx) / (2.0f * kPi);
        const int i = y * canvas.width() + x;
        angle_[i] = static_cast<uint8_t>(std::lround(turns * 256.0f) & 0xFF);
        const float distance = farthest > 0.0f ? std::sqrt(dx * dx + dy * dy) / farthest : 0.0f;
        distance_[i] = static_cast<uint8_t>(std::lround(distance * 255.0f));
      }
    }
    dirty_ = true;
  }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;

    // Phase in 8.8 palette steps; 64-bit because nowMs * speed * 256 overflows 32 bits
    // after a few hours. Only the low 16 bits matter: the index wraps at 256 steps.
    const uint32_t phase =
        static_cast<uint32_t>((static_cast<uint64_t>(ctx.nowMs) * speed * 256u / 1000u) & 0xFFFFu);
    const bool clockwise = ctx.params[kDirection] == kClockwise;
    // A quarter turn is 64 palette steps: distance (0..255) * twist * 64 / 255 steps,
    // kept in 8.8 like the phase.
    const uint32_t twist = static_cast<uint32_t>(ctx.params[kTwist]);
    Canvas& canvas = ctx.canvas;
    CRGB* pixels = canvas.data();
    for (int i = 0; i < canvas.size(); ++i) {
      const uint32_t spiral =
          (static_cast<uint32_t>(angle_[i]) << 8) + distance_[i] * twist * 64u * 256u / 255u;
      // Clockwise: a color moves towards larger angles, so the phase is subtracted.
      const uint32_t index = clockwise ? spiral - phase : spiral + phase;
      pixels[i] = ColorFromPalette(ctx.palette, static_cast<uint8_t>(index >> 8), 255, LINEARBLEND);
    }
    return true;
  }

 private:
  uint8_t angle_[ledframe::kMaxLeds] = {};     // 1/256 turn around the center, row by row
  uint8_t distance_[ledframe::kMaxLeds] = {};  // 1/255 of the distance to the farthest corner
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(SwirlEffect, "swirl", "Swirl", 56, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

// Rainbow rings from the center (v1 idea "rainbow from center").
//
// Every pixel takes the palette color at its distance from the center, scaled by
// `density`, shifted by a phase that grows with time:
//   index = distance * density +/- nowMs * speed
// The distances are computed once in begin() (12.4 fixed point, 2 KB); render() uses
// only integer math. A layout change creates the effect again, so they always match.
//   speed      palette steps per second (256 steps = one full palette); 0 = still
//   density    palette steps per pixel of distance
//   direction  outward: rings move away from the center; inward: towards it
//   palette    ring colors
#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kDensity, kDirection, kPalette };
enum Direction : int32_t { kOutward, kInward };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 64},
    {"density", "Density", ParamKind::Slider, 1, 64, 16},
    {"direction", "Direction", ParamKind::Select, 0, 0, kOutward, "outward|inward"},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

class RadialRainbowEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    const Canvas& canvas = ctx.canvas;
    const float centerX = (canvas.width() - 1) / 2.0f;
    const float centerY = (canvas.height() - 1) / 2.0f;
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        const float dx = x - centerX;
        const float dy = y - centerY;
        // At most 512 pixels (1024 x 1 strip): 512 * 16 fits in 16 bits.
        const float distance = std::sqrt(dx * dx + dy * dy);
        distance_[y * canvas.width() + x] = static_cast<uint16_t>(std::lround(distance * 16.0f));
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
    // after a few hours.
    const uint32_t phase =
        static_cast<uint32_t>((static_cast<uint64_t>(ctx.nowMs) * speed * 256u / 1000u) & 0xFFFFu);
    const bool inward = ctx.params[kDirection] == kInward;
    const uint32_t density = static_cast<uint32_t>(ctx.params[kDensity]);
    Canvas& canvas = ctx.canvas;
    CRGB* pixels = canvas.data();
    for (int i = 0; i < canvas.size(); ++i) {
      const uint32_t rings = (distance_[i] * density) << 4;  // 12.4 -> 8.8 palette steps
      // Outward: a ring keeps its color while it moves away, so the phase is subtracted.
      const uint32_t index = inward ? rings + phase : rings - phase;
      pixels[i] = ColorFromPalette(ctx.palette, static_cast<uint8_t>(index >> 8), 255, LINEARBLEND);
    }
    return true;
  }

 private:
  uint16_t distance_[ledframe::kMaxLeds] = {};  // 12.4 pixels from the center, row by row
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(RadialRainbowEffect, "radial-rainbow", "Radial rainbow", 55,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

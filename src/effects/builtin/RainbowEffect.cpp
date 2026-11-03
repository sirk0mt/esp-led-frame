// Palette gradient at an angle (v1 "Rainbow" / rainbow flow).
//
// Every pixel takes the palette color at its position along the direction of
// `angle`, scaled by `density`, shifted by a phase that grows with time:
//   index = (x * cos(angle) + y * sin(angle)) * density + nowMs * speed
// All per-pixel math is integer: the direction is 8.8 fixed point, computed only
// when the angle changes.
//   speed    palette steps per second (256 steps = one full palette); 0 = still
//   angle    0 = gradient along x (columns share a color), 90 = along y
//   density  palette steps per pixel
#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kAngle, kDensity, kPalette };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 64},
    {"angle", "Angle", ParamKind::Slider, 0, 359, 0},
    {"density", "Density", ParamKind::Slider, 1, 64, 10},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr float kPi = 3.14159265f;
constexpr int32_t kOne = 256;  // 1.0 in 8.8 fixed point

class RainbowEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    setDirection(ctx.params[kAngle]);
    dirty_ = true;
  }

  void onParamChanged(uint8_t index, FrameContext& ctx) override {
    if (index == kAngle) {
      setDirection(ctx.params[kAngle]);
    }
    dirty_ = true;
  }

  bool render(FrameContext& ctx) override {
    const int32_t speed = ctx.params[kSpeed];
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;

    // Phase in 8.8 palette steps. 64-bit math: nowMs * speed * 256 overflows 32 bits
    // after a few hours.
    const uint32_t phase = static_cast<uint32_t>(
        (static_cast<uint64_t>(ctx.nowMs) * static_cast<uint64_t>(speed) * kOne / 1000u) & 0xFFFFu);
    const int32_t density = ctx.params[kDensity];
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int32_t position = x * dx_ + y * dy_;  // pixels along the direction, 8.8
        const uint32_t index = static_cast<uint32_t>(position * density) + phase;
        pixels[y * width + x] =
            ColorFromPalette(ctx.palette, static_cast<uint8_t>(index >> 8), 255, LINEARBLEND);
      }
    }
    return true;
  }

 private:
  void setDirection(int32_t degrees) {
    const float radians = static_cast<float>(degrees) * kPi / 180.0f;
    dx_ = static_cast<int32_t>(std::lround(std::cos(radians) * kOne));
    dy_ = static_cast<int32_t>(std::lround(std::sin(radians) * kOne));
  }

  int32_t dx_ = kOne;
  int32_t dy_ = 0;
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(RainbowEffect, "rainbow", "Rainbow", 50, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

// Noise: soft blobs that drift up and change shape, like wax in a lava lamp.
//
// Every pixel takes the palette color of 3D Perlin noise (FastLED inoise8) at its
// position, with time as the third axis. The picture also rises slowly, so blobs
// move up while they melt and form again:
//   index = stretch(inoise8(x * scale, y * scale + rise, t))
// rise grows by 2 * speed and t by speed / 2 noise units per second (256 units = one
// noise cell): the blobs rise faster than they change, or the change would hide
// the rise (speed 40, scale 30: about 2.7 pixels per second up). The noise repeats
// every 256 cells on each axis, which matches the 16-bit wrap of each coordinate, so
// time runs on without a jump. inoise8 rarely leaves 32..224; stretch() spreads that
// range over the whole palette.
//   speed    how fast the blobs change and rise; 0 = still
//   scale    noise units per pixel: higher gives smaller blobs
//   palette  blob colors (default: lava; ocean and forest also suit)
#include <lib8tion.h>
#include <noise.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kScale, kPalette };

constexpr int32_t kLavaPalette = 4;  // render/Palettes.cpp: rainbow, party, ocean, forest, lava

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 40},
    {"scale", "Scale", ParamKind::Slider, 5, 100, 30},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kLavaPalette},
};

// Maps the usual inoise8 range 32..224 to 0..255.
uint8_t stretch(uint8_t noise) {
  const int32_t value = (static_cast<int32_t>(noise) - 32) * 4 / 3;
  return static_cast<uint8_t>(value < 0 ? 0 : value > 255 ? 255 : value);
}

class NoiseEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override { dirty_ = true; }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;

    // 64-bit: nowMs * speed overflows 32 bits after a few hours. Both coordinates come
    // from the 64-bit count before the cut to 16 bits, which is seamless (see above);
    // scaling an already cut value would jump at its wrap.
    const uint64_t halfUnits = static_cast<uint64_t>(ctx.nowMs) * speed / 2000u;
    const uint16_t t = static_cast<uint16_t>(halfUnits);
    const uint16_t rise = static_cast<uint16_t>(halfUnits * 4u);
    const uint16_t scale = static_cast<uint16_t>(ctx.params[kScale]);
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height; ++y) {
      const uint16_t noiseY = static_cast<uint16_t>(y * scale + rise);
      for (int x = 0; x < width; ++x) {
        const uint8_t noise = inoise8(static_cast<uint16_t>(x * scale), noiseY, t);
        pixels[y * width + x] = ColorFromPalette(ctx.palette, stretch(noise), 255, LINEARBLEND);
      }
    }
    return true;
  }

 private:
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(NoiseEffect, "noise", "Noise", 57, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

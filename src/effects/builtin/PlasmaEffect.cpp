// Plasma: overlapping sine waves, the classic demo effect.
//
// Three waves cross the matrix: one along x, one along y and one along the
// diagonal, each moving at its own rate. Their mean picks the palette color, and
// the colors also cycle slowly, so the picture never repeats a simple shift:
//   wave(x)   = sin8(x * scale + 2t)
//   wave(y)   = sin8(y * scale - 3t)
//   wave(x+y) = sin8((x + y) * scale / 2 + t)
//   index     = (wave(x) + wave(y) + wave(x+y)) / 3 + t
// t is an angle (256 = one turn) that grows by 2 * speed per second. Only integer
// lib8tion math, no buffer.
//   speed    how fast the waves move; 0 = still
//   scale    wave steps per pixel: higher gives smaller, busier blobs
//   palette  plasma colors (default: party)
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kScale, kPalette };

constexpr int32_t kPartyPalette = 1;  // render/Palettes.cpp: rainbow, party, ...

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 32},
    {"scale", "Scale", ParamKind::Slider, 1, 64, 24},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kPartyPalette},
};

class PlasmaEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override { dirty_ = true; }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;

    // Only the low 8 bits matter (sin8 takes an angle), and multiples of t wrap
    // consistently, so the 64-bit product can be cut to 8 bits. 64-bit because
    // nowMs * speed overflows 32 bits after a few hours.
    const uint8_t t = static_cast<uint8_t>(static_cast<uint64_t>(ctx.nowMs) * speed * 2u / 1000u);
    const uint8_t scale = static_cast<uint8_t>(ctx.params[kScale]);
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height; ++y) {
      const uint8_t waveY = sin8(static_cast<uint8_t>(y * scale - 3 * t));
      for (int x = 0; x < width; ++x) {
        const uint8_t waveX = sin8(static_cast<uint8_t>(x * scale + 2 * t));
        const uint8_t waveXY = sin8(static_cast<uint8_t>((x + y) * scale / 2 + t));
        const uint8_t index = static_cast<uint8_t>((waveX + waveY + waveXY) / 3 + t);
        pixels[y * width + x] = ColorFromPalette(ctx.palette, index, 255, LINEARBLEND);
      }
    }
    return true;
  }

 private:
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(PlasmaEffect, "plasma", "Plasma", 52, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

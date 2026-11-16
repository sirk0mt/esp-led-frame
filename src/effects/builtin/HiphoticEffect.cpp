// Hiphotic: palette bands bent into flowing, swelling shapes by nested sine waves.
//
// Adapted from "Hiphotic" in WLED (wled00/FX.cpp, mode_2DHiphotic; by ldirko,
// https://editor.soulmatelights.com/gallery/810, modified by Andrew Tuline). Written
// again for this firmware; the picture and the options follow WLED.
//
// Every pixel takes the palette color at a sine of two waves and the time a:
//   index = sin(cos(x * x_scale / 16 + a / 3) + sin(y * y_scale / 16 + a / 4) + a)
// The wave along x moves at a third of the pace and the wave along y at a quarter, so
// the shapes keep changing while the colors run through them.
//
// The picture is a function of time only, so it looks the same at any FPS. Unlike WLED,
// which counts in 1/256 turns and palette steps (sin8) and time in whole steps, the
// angles and the palette position have 16 bits (sin16) and time runs on in fractions.
// At low speed WLED's colors change in visible steps; here they flow.
//   speed     pace in percent of WLED's fastest (WLED's speed slider is a divisor
//             1-16, slower when higher; its default 9 is 11 here); 0 = still
//   x_scale   wave steps per column: higher gives narrower shapes across
//   y_scale   the same per row
//   palette   colors
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kXScale, kYScale, kPalette };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 100, 11},
    {"x_scale", "X scale", ParamKind::Slider, 0, 255, 128},
    {"y_scale", "Y scale", ParamKind::Slider, 0, 255, 128},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kMaxStepMs = 1000;  // a long stall moves the picture at most 1 s on

// sin and cos as 0..65535, for angles in 1/65536 turns: sin8 and cos8 of WLED times 256.
inline uint32_t sine(uint32_t angle) {
  return static_cast<uint32_t>(sin16(static_cast<uint16_t>(angle)) + 32768);
}
inline uint32_t cosine(uint32_t angle) {
  return static_cast<uint32_t>(cos16(static_cast<uint16_t>(angle)) + 32768);
}

// Palette color at a 16-bit position: as ColorFromPalette with LINEARBLEND, but with 12
// bits between two palette entries instead of 4, so a slow change has no visible steps.
inline CRGB paletteColor(const CRGBPalette16& palette, uint32_t position) {
  const uint32_t entry = (position >> 12) & 15u;
  const uint32_t blend = position & 0xFFFu;
  const CRGB& from = palette.entries[entry];
  const CRGB& to = palette.entries[(entry + 1u) & 15u];
  return CRGB(static_cast<uint8_t>((from.r * (4096u - blend) + to.r * blend) >> 12),
              static_cast<uint8_t>((from.g * (4096u - blend) + to.g * blend) >> 12),
              static_cast<uint8_t>((from.b * (4096u - blend) + to.b * blend) >> 12));
}

class HiphoticEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override {
    clock_ = 0;
    dirty_ = true;
  }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;
    const uint32_t deltaMs = ctx.deltaMs < kMaxStepMs ? ctx.deltaMs : kMaxStepMs;
    clock_ += static_cast<uint64_t>(deltaMs) * speed;  // WLED's a in 1/100 steps

    // WLED: a, a / 3 and a / 4 as angles in 1/256 turns. Here in 1/65536 turns with the
    // fractions, from the full 64-bit clock so that a / 3 does not jump when a wraps.
    const uint32_t a = static_cast<uint32_t>(clock_ * 256u / 100u);
    const uint32_t a3 = static_cast<uint32_t>(clock_ * 256u / 300u);
    const uint32_t a4 = static_cast<uint32_t>(clock_ * 256u / 400u);
    // WLED: x * scale / 16 in 1/256 turns; here x * scale * 16 in 1/65536 turns.
    const uint32_t stepX = static_cast<uint32_t>(ctx.params[kXScale]) * 16u;
    const uint32_t stepY = static_cast<uint32_t>(ctx.params[kYScale]) * 16u;

    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height; ++y) {
      const uint32_t waveY = sine(static_cast<uint32_t>(y) * stepY + a4) + a;
      CRGB* row = pixels + y * width;
      for (int x = 0; x < width; ++x) {
        const uint32_t waveX = cosine(static_cast<uint32_t>(x) * stepX + a3);
        row[x] = paletteColor(ctx.palette, sine(waveX + waveY));
      }
    }
    return true;
  }

 private:
  uint64_t clock_ = 0;  // WLED's time a in 1/100 steps: deltaMs * speed
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(HiphoticEffect, "hiphotic", "Hiphotic", 51,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

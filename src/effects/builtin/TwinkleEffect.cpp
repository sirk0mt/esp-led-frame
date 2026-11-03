// Twinkle: stars that softly light up and fade out, in the style of TwinkleFOX.
//
// The effect keeps no state: the color of a pixel follows from a hash of its index
// and the time, so any moment can be drawn on its own. Every pixel runs its own
// clock, with a phase offset and a rate of 0.75..1.7 times the base rate, both from
// a hash of the index, so the pixels never fall into step:
//   phase = nowMs * speed / 60000 * rate + offset      (in cycles)
// In every cycle a hash of (index, cycle number) decides whether the pixel lights up
// (with chance density %) and with which palette color. A lit pixel rises in the
// first quarter of the cycle and fades in the rest; the brightness is squared, so it
// starts and ends gently. Both ends of a cycle are dark, so a new cycle never jumps.
//   density     chance in % that a pixel lights up in one of its cycles
//   speed       base twinkle cycles per pixel per minute
//   palette     star colors (default: party)
//   background  color of the dark pixels; stars blend over it (default: black)
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kDensity, kSpeed, kPalette, kBackground };

constexpr int32_t kPartyPalette = 1;  // render/Palettes.cpp: rainbow, party, ...

constexpr ParamSpec kParams[] = {
    {"density", "Density", ParamKind::Slider, 1, 100, 40},
    {"speed", "Speed", ParamKind::Slider, 1, 100, 15},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kPartyPalette},
    {"background", "Background", ParamKind::Color, 0, 0xFFFFFF, 0x000000},
};

// The per-pixel rate is kRateMin..kRateMin + 15 sixteenths of the base rate.
constexpr uint32_t kRateMin = 12;
// Phase units (1/65536 cycle) per ms at speed 1 and rate 1/16, as a fraction:
// 65536 / (60000 * 16).
constexpr uint64_t kPhaseNum = 65536;
constexpr uint64_t kPhaseDen = 60000ull * 16u;

// 32-bit integer hash (lowbias32 by Chris Wellons): every input bit changes about
// half of the output bits.
uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Brightness 0..255 at position frac (1/65536 cycle): quick rise, slow fade.
uint8_t twinkleLevel(uint16_t frac) {
  constexpr uint32_t kRise = 16384;  // the first quarter of the cycle
  const uint32_t wave = frac < kRise ? frac * 4u : 65535u - (frac - kRise) * 4u / 3u;
  return static_cast<uint8_t>((wave * wave) >> 24);
}

class TwinkleEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    // 64-bit: nowMs * speed * kPhaseNum needs about 59 bits. The base is computed once
    // per frame, so pixels only multiply and add.
    const uint64_t base = static_cast<uint64_t>(ctx.nowMs) *
                          static_cast<uint32_t>(ctx.params[kSpeed]) * kPhaseNum / kPhaseDen;
    const uint32_t threshold = static_cast<uint32_t>(ctx.params[kDensity]) * 65536u / 100u;
    const CRGB background = ctx.userColor(ctx.params[kBackground]);
    Canvas& canvas = ctx.canvas;
    const uint16_t size = canvas.size();
    CRGB* pixels = canvas.data();
    for (uint16_t i = 0; i < size; ++i) {
      const uint32_t pixelHash = hash(i * 0x9E3779B9u + 0x5EEDu);
      const uint64_t offset = static_cast<uint64_t>(pixelHash & 0xFFFFu) << 8;
      const uint32_t rate = kRateMin + ((pixelHash >> 16) & 0x0Fu);
      const uint64_t phase = base * rate + offset;
      const uint32_t cycle = static_cast<uint32_t>(phase >> 16);
      const uint32_t cycleHash = hash(pixelHash ^ (cycle * 0x85EBCA6Bu));

      pixels[i] = background;
      if ((cycleHash & 0xFFFFu) < threshold) {
        const uint8_t level = twinkleLevel(static_cast<uint16_t>(phase));
        const CRGB star =
            ColorFromPalette(ctx.palette, static_cast<uint8_t>(cycleHash >> 24), 255, LINEARBLEND);
        nblend(pixels[i], star, level);
      }
    }
    return true;
  }
};

}  // namespace

REGISTER_EFFECT(TwinkleEffect, "twinkle", "Twinkle", 65, ledframe::kFlagPalette, kParams);

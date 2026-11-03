// Breathe: the whole matrix in one color, slowly getting brighter and darker.
//
// The brightness follows a sine from min level up to full and back once per period,
// starting at min level. The sine is squared before scaling, because the eye sees
// LED brightness about as the square root of its PWM level: squared, the breath
// looks even instead of lingering at the bright end:
//   level = min + (255 - min) * ((1 - cos(2 pi t / period)) / 2)^2
// With colors = palette the color also walks through the palette, once in
// kPaletteBreaths breaths, so one breath shows only a small color shift.
//   colors     color: the color parameter; palette: colors from the palette
//   color      breathing color for colors = color (default: warm white)
//   period     length of one breath in ms
//   min_level  darkest level 0..255; 255 gives a steady light
//   palette    palette for colors = palette
#include <lib8tion.h>

#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kColors, kColor, kPeriod, kMinLevel, kPalette };
enum Colors : int32_t { kColorsColor, kColorsPalette };

constexpr ParamSpec kParams[] = {
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsColor, "color|palette"},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFA050},
    {"period", "Period", ParamKind::Slider, 1000, 30000, 5000},
    {"min_level", "Min level", ParamKind::Slider, 0, 255, 24},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

// Breaths for one walk through the palette.
constexpr uint32_t kPaletteBreaths = 8;

class BreatheEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override { dirty_ = true; }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    // One float cosine per frame, not per pixel: exact at the peak, unlike cos16, which
    // stops short of -1 and would never give full brightness.
    const uint32_t period = static_cast<uint32_t>(ctx.params[kPeriod]);
    const float turn = static_cast<float>(ctx.nowMs % period) / static_cast<float>(period);
    const float wave = 0.5f - 0.5f * std::cos(turn * 6.2831853f);
    const float minLevel = static_cast<float>(ctx.params[kMinLevel]);
    const uint8_t level = static_cast<uint8_t>(minLevel + (255.0f - minLevel) * wave * wave + 0.5f);

    CRGB color;
    if (ctx.params[kColors] == kColorsPalette) {
      const uint32_t walkMs = period * kPaletteBreaths;
      const uint8_t index = static_cast<uint8_t>((ctx.nowMs % walkMs) * 256u / walkMs);
      color = ColorFromPalette(ctx.palette, index, level, LINEARBLEND);
    } else {
      const CRGB full = ctx.userColor(ctx.params[kColor]);
      color = CRGB(scale8(full.r, level), scale8(full.g, level), scale8(full.b, level));
    }

    if (!dirty_ && color == last_) {
      return false;  // Slow breaths keep the same 8-bit color for several frames.
    }
    dirty_ = false;
    last_ = color;
    ctx.canvas.fill(color);
    return true;
  }

 private:
  bool dirty_ = true;
  CRGB last_;
};

}  // namespace

REGISTER_EFFECT(BreatheEffect, "breathe", "Breathe", 15, ledframe::kFlagPalette, kParams);

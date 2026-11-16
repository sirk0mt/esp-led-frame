// Octopus: waving arms of light around a center, colored in rings from the palette.
//
// Adapted from "Octopus" in WLED (wled00/FX.cpp, mode_2Doctopus; by Stepko and
// Sutaburosu, https://editor.soulmatelights.com/gallery/671-octopus, adapted for WLED by
// blazoncek). Written again for this firmware; the picture and the options follow WLED.
//
// Every pixel knows its angle around the center and its distance from it (computed once
// in begin() and when the center moves, one byte each, as in WLED). With the time t:
//   brightness = sin(sin(angle - radius / 4 + t / 2) + radius - t + angle * legs)
//   color      = palette at t / 2 - radius
// The angle term makes `legs` bright arms, the inner sine bends them, and the radius and
// time terms make them wave outwards, while the palette runs outwards in rings.
//
// The picture is a function of time only, so it looks the same at any FPS. Unlike WLED,
// which counts angles, brightness phase and palette steps in 8 bits (sin8) and time in
// whole steps per frame, the phases and the palette position have 16 bits (sin16) and
// time runs on in fractions. At low speed WLED's picture changes in visible steps; here
// it flows. The center is the middle of the matrix; WLED's (width / 2, height / 2) is
// half a pixel off the middle on an even side.
//   speed     pace in quarters of WLED's steps per frame (WLED: 1-8 steps, its default
//             5 is 20 here); 0 = still
//   legs      number of arms
//   leg_size  width of the arms: 50 is WLED's (the brightness is a plain sine around
//             the center); lower cuts the dark part of the wave off, so the arms get
//             thinner with dark gaps between them; higher cuts the bright part off, so
//             the arms get thicker and flat on top, with narrow gaps
//   offset_x  center across: 128 is the middle, 0 and 255 half a width to either side
//   offset_y  the same up and down
//   palette   ring colors
#include <lib8tion.h>

#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kLegs, kLegSize, kOffsetX, kOffsetY, kPalette };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 32, 20},
    {"legs", "Legs", ParamKind::Slider, 1, 8, 5},
    {"leg_size", "Leg size", ParamKind::Slider, 0, 100, 50},
    {"offset_x", "Offset X", ParamKind::Slider, 0, 255, 128},
    {"offset_y", "Offset Y", ParamKind::Slider, 0, 255, 128},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kMaxStepMs = 1000;  // a long stall moves the picture at most 1 s on
constexpr float kPi = 3.14159265f;

// sin as 0..65535, for angles in 1/65536 turns: sin8 of WLED times 256.
inline uint32_t sine(uint32_t angle) {
  return static_cast<uint32_t>(sin16(static_cast<uint16_t>(angle)) + 32768);
}

// Palette color at a 16-bit position, dimmed to level / 65536: as ColorFromPalette with
// LINEARBLEND, but with 12 bits between two palette entries instead of 4, so a slow
// change has no visible steps.
inline CRGB paletteColor(const CRGBPalette16& palette, uint32_t position, uint32_t level) {
  const uint32_t entry = (position >> 12) & 15u;
  const uint32_t blend = position & 0xFFFu;
  const CRGB& from = palette.entries[entry];
  const CRGB& to = palette.entries[(entry + 1u) & 15u];
  // A channel times 4096 has 20 bits; with the level shifted to 12 bits it fits 32.
  const uint32_t scale = level >> 4;
  return CRGB(
      static_cast<uint8_t>(((from.r * (4096u - blend) + to.r * blend) >> 12) * scale >> 12),
      static_cast<uint8_t>(((from.g * (4096u - blend) + to.g * blend) >> 12) * scale >> 12),
      static_cast<uint8_t>(((from.b * (4096u - blend) + to.b * blend) >> 12) * scale >> 12));
}

class OctopusEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    clock_ = 0;
    buildMap(ctx);
  }

  void onParamChanged(uint8_t index, FrameContext& ctx) override {
    if (index == kOffsetX || index == kOffsetY) {
      buildMap(ctx);
    }
    dirty_ = true;
  }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;
    const uint32_t deltaMs = ctx.deltaMs < kMaxStepMs ? ctx.deltaMs : kMaxStepMs;
    clock_ += static_cast<uint64_t>(deltaMs) * speed;

    // WLED: t grows by 1-8 per frame at 42 frames per second, in 1/256 turns. Here in
    // 1/65536 turns: speed / 4 * 42 * 256 per second. t / 2 comes from the full 64-bit
    // clock so that it does not jump when t wraps.
    const uint32_t t = static_cast<uint32_t>(clock_ * 2688u / 1000u);
    const uint32_t tHalf = static_cast<uint32_t>(clock_ * 1344u / 1000u);
    const uint32_t legs = static_cast<uint32_t>(ctx.params[kLegs]);

    // Leg size: the brightness wave (0..65535) is cut at a low mark and stretched by a
    // gain (8.8), then capped. Below 50 the mark rises to 85 % of the wave and the gain
    // keeps the peak full; above 50 the gain alone rises, up to 1 / 0.15.
    const int32_t size = ctx.params[kLegSize];
    const uint32_t cut =
        static_cast<uint32_t>((size < 50 ? 50 - size : size - 50) * 65536 / 50 * 85 / 100);
    const uint32_t low = size < 50 ? cut : 0u;
    const uint32_t gain = 65536u * 256u / (65536u - cut);

    Canvas& canvas = ctx.canvas;
    CRGB* pixels = canvas.data();
    const int count = canvas.size();
    for (int i = 0; i < count; ++i) {
      const uint32_t angle = static_cast<uint32_t>(angle_[i]) << 8;
      const uint32_t radius = static_cast<uint32_t>(radius_[i]) << 8;
      const uint32_t bend = sine(angle - radius / 4u + tHalf);
      const uint32_t wave = sine(bend + radius - t + angle * legs);
      const uint32_t shaped = wave > low ? ((wave - low) * gain) >> 8 : 0u;
      const uint32_t level = shaped < 65535u ? shaped : 65535u;
      pixels[i] = paletteColor(ctx.palette, tHalf - radius, level);
    }
    return true;
  }

 private:
  // Angle and distance of every pixel from the center (atan2 and sqrt, so only here).
  void buildMap(FrameContext& ctx) {
    const Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    const float centerX = static_cast<float>(width - 1) / 2.0f +
                          static_cast<float>((ctx.params[kOffsetX] - 128) * width) / 255.0f;
    const float centerY = static_cast<float>(height - 1) / 2.0f +
                          static_cast<float>((ctx.params[kOffsetY] - 128) * height) / 255.0f;
    // WLED: 180 / max(cols, rows) radius steps per pixel, a whole number.
    const float perPixel = static_cast<float>(180 / (width > height ? width : height));
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const float dx = static_cast<float>(x) - centerX;
        const float dy = static_cast<float>(y) - centerY;
        const int i = y * width + x;
        // 1/256 turns, as WLED's 40.7436 * atan2 (128 / pi).
        angle_[i] = static_cast<uint8_t>(std::lround(std::atan2(dy, dx) * 128.0f / kPi) & 0xFF);
        // Wraps past 255 on the far side of a moved center, as in WLED.
        radius_[i] =
            static_cast<uint8_t>(static_cast<int>(std::sqrt(dx * dx + dy * dy) * perPixel) & 0xFF);
      }
    }
    dirty_ = true;
  }

  uint8_t angle_[ledframe::kMaxLeds] = {};   // 1/256 turn around the center, row by row
  uint8_t radius_[ledframe::kMaxLeds] = {};  // distance from the center, in radius steps
  uint64_t clock_ = 0;                       // deltaMs * speed
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(OctopusEffect, "octopus", "Octopus", 63, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

// Fire rising from the bottom row (an idea from v1, Fire2012 style).
//
// Every column has its own heat values. One simulation step cools every cell a
// little, lets heat drift up, and lights random sparks near the bottom. Steps run
// at a fixed rate counted from deltaMs, so the fire looks the same at any FPS. The
// heat picks the color from the palette (default: heat, black to red to white).
//   speed     simulation steps per second
//   cooling   how fast the flames cool down: higher gives shorter flames
//   sparking  chance of a new spark per column and step, out of 255
//   palette   colors from cold (index 0) to hot (index 240)
// Randomness comes from FastLED random8(); App seeds it at start.
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kCooling, kSparking, kPalette };

constexpr int32_t kHeatPalette = 5;  // render/Palettes.cpp: rainbow, party, ocean, forest, lava, heat

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 10, 100, 60},
    {"cooling", "Cooling", ParamKind::Slider, 20, 100, 55},
    {"sparking", "Sparking", ParamKind::Slider, 20, 255, 120},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kHeatPalette},
};

constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall the fire catches up only a little
constexpr uint8_t kSparkRows = 3;         // sparks start in the lowest rows

class FireEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    width_ = ctx.canvas.width();
    height_ = ctx.canvas.height();
    for (uint8_t& cell : heat_) {
      cell = 0;
    }
    carryMs_ = 0;
  }

  bool render(FrameContext& ctx) override {
    const uint32_t stepMs = 1000u / static_cast<uint32_t>(ctx.params[kSpeed]);
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= stepMs) {
      carryMs_ -= stepMs;
      if (steps < kMaxStepsPerFrame) {
        step(static_cast<uint8_t>(ctx.params[kCooling]),
             static_cast<uint8_t>(ctx.params[kSparking]));
        ++steps;
      }
    }
    if (steps == 0) {
      return false;  // Same heat as in the last frame.
    }

    // Heat index i counts from the bottom; the canvas has y = 0 at the top.
    Canvas& canvas = ctx.canvas;
    for (int x = 0; x < width_; ++x) {
      const uint8_t* column = &heat_[x * height_];
      for (int i = 0; i < height_; ++i) {
        canvas.at(x, height_ - 1 - i) =
            ColorFromPalette(ctx.palette, scale8(column[i], 240), 255, LINEARBLEND);
      }
    }
    return true;
  }

 private:
  void step(uint8_t cooling, uint8_t sparking) {
    // Fire2012: shorter matrices cool faster per cell, so flames keep their proportion.
    const int perCell = cooling * 10 / height_ + 2;
    const uint8_t maxCooling = static_cast<uint8_t>(perCell > 255 ? 255 : perCell);
    for (int x = 0; x < width_; ++x) {
      uint8_t* column = &heat_[x * height_];
      for (int i = 0; i < height_; ++i) {
        column[i] = qsub8(column[i], random8(maxCooling));
      }
      // Heat drifts up and spreads a little.
      for (int i = height_ - 1; i >= 2; --i) {
        column[i] = static_cast<uint8_t>((column[i - 1] + column[i - 2] + column[i - 2]) / 3);
      }
      if (random8() < sparking) {
        const int i = random8(static_cast<uint8_t>(height_ < kSparkRows ? height_ : kSparkRows));
        column[i] = qadd8(column[i], random8(160, 255));
      }
    }
  }

  int width_ = 0;
  int height_ = 0;
  uint32_t carryMs_ = 0;
  uint8_t heat_[ledframe::kMaxLeds] = {};  // heat_[x * height + i], i from the bottom
};

}  // namespace

REGISTER_EFFECT(FireEffect, "fire", "Fire", 75, ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

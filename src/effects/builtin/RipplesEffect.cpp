// Ripples: drops falling into water, with rings that spread, bounce and calm down.
//
// The water is a height field on the wave equation in its classic two-buffer form:
//   next = (left + right + up + down) / 2 - previous, then damped
// The buffers hold the current and the previous height of every pixel. Outside the
// matrix the water stays flat, so rings fade at the edges. One step every kStepMs
// moves the waves by about a pixel and drops new stones at random; steps run at a
// fixed rate counted from deltaMs, so the water looks the same at any FPS. The
// picture is the height interpolated between the last two steps, so the rings move
// smoothly, and the height picks the palette color: 128 is calm water, crests are
// higher indexes and troughs lower ones.
//
// Heights are int8_t: two buffers of 1 byte per pixel fit the effect slot (2 bytes
// per LED). Damping multiplies by (256 - damping) / 256 and rounds towards zero,
// so every wave dies out in the end instead of leaving small ripples forever.
//   density  new drops per minute on average; 0 = calm water
//   damping  how fast the waves calm down
//   palette  water colors (default: ocean)
// Randomness comes from FastLED random8() and random16(); App seeds them at start.
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kDensity, kDamping, kPalette };

constexpr int32_t kOceanPalette = 2;  // render/Palettes.cpp: rainbow, party, ocean

constexpr ParamSpec kParams[] = {
    {"density", "Density", ParamKind::Slider, 0, 240, 60},
    {"damping", "Damping", ParamKind::Slider, 1, 64, 8},
    {"palette", "Palette", ParamKind::Palette, 0, 0, kOceanPalette},
};

constexpr uint32_t kStepMs = 40;           // 25 steps per second
constexpr uint8_t kMaxStepsPerFrame = 10;  // after a stall the water catches up only a little
// A drop is a soft 3 x 3 dent: kDropDepth in the middle, half of it on the sides and
// a quarter on the corners. A single-pixel dent would mostly excite the shortest
// waves the grid can carry, which show up as a checkerboard.
constexpr int32_t kDropDepth = 80;
constexpr int32_t kGain = 3;  // palette steps per height unit

int8_t clampHeight(int32_t value) {
  return static_cast<int8_t>(value < -127 ? -127 : value > 127 ? 127 : value);
}

class RipplesEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    width_ = ctx.canvas.width();
    height_ = ctx.canvas.height();
    for (auto& heights : heights_) {
      for (int8_t& h : heights) {
        h = 0;
      }
    }
    current_ = 0;
    carryMs_ = 0;
    calmDrawn_ = false;
  }

  bool render(FrameContext& ctx) override {
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      if (steps < kMaxStepsPerFrame) {
        step(static_cast<uint32_t>(ctx.params[kDensity]),
             static_cast<int32_t>(ctx.params[kDamping]));
        ++steps;
      }
    }

    const int8_t* now = heights_[current_];
    const int8_t* before = heights_[1 - current_];
    const int size = width_ * height_;
    bool calm = true;
    for (int i = 0; i < size && calm; ++i) {
      calm = now[i] == 0 && before[i] == 0;
    }
    if (calm && calmDrawn_) {
      return false;  // Flat water, already on the canvas.
    }
    calmDrawn_ = calm;

    // How far the water is from the last step to the new one, 0..255.
    const int32_t phase = static_cast<int32_t>(carryMs_ * 256u / kStepMs);
    CRGB* pixels = ctx.canvas.data();
    for (int i = 0; i < size; ++i) {
      // Height in 1/256 units, between the last two steps.
      const int32_t h = before[i] * 256 + (now[i] - before[i]) * phase;
      const int32_t index = 128 + h * kGain / 256;
      // NOWRAP: high crests stay at the last palette entry instead of blending back
      // towards the first one.
      pixels[i] = ColorFromPalette(ctx.palette,
                                   static_cast<uint8_t>(index < 0     ? 0
                                                        : index > 255 ? 255
                                                                      : index),
                                   255, LINEARBLEND_NOWRAP);
    }
    return true;
  }

 private:
  // One step of the wave equation into the buffer of the older height, then drops.
  void step(uint32_t density, int32_t damping) {
    const int8_t* now = heights_[current_];
    int8_t* next = heights_[1 - current_];  // holds the height one step before now
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        const int i = y * width_ + x;
        const int32_t sum = (x > 0 ? now[i - 1] : 0) + (x + 1 < width_ ? now[i + 1] : 0) +
                            (y > 0 ? now[i - width_] : 0) + (y + 1 < height_ ? now[i + width_] : 0);
        const int32_t wave = sum / 2 - next[i];
        // Division rounds towards zero, so small waves reach 0 instead of lingering.
        next[i] = clampHeight(wave * (256 - damping) / 256);
      }
    }
    current_ = 1 - current_;

    // density drops per minute: density / (60000 / kStepMs) per step.
    const uint32_t chance = density * 65536u / (60000u / kStepMs);
    if (random16() < chance) {
      const uint32_t size = static_cast<uint32_t>(width_ * height_);
      const uint32_t at = static_cast<uint32_t>(random16()) * size >> 16;
      drop(static_cast<int>(at % static_cast<uint32_t>(width_)),
           static_cast<int>(at / static_cast<uint32_t>(width_)));
    }
  }

  void drop(int x0, int y0) {
    int8_t* now = heights_[current_];
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const int x = x0 + dx;
        const int y = y0 + dy;
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
          continue;
        }
        // Middle: full depth; sides: half; corners: a quarter.
        const int32_t depth = kDropDepth >> ((dx != 0) + (dy != 0));
        const int i = y * width_ + x;
        now[i] = clampHeight(now[i] - depth);
      }
    }
  }

  int width_ = 0;
  int height_ = 0;
  uint32_t carryMs_ = 0;
  int current_ = 0;
  bool calmDrawn_ = false;                      // the canvas shows flat water
  int8_t heights_[2][ledframe::kMaxLeds] = {};  // current and previous height
};

}  // namespace

REGISTER_EFFECT(RipplesEffect, "ripples", "Ripples", 58, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);

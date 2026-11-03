// Whole canvas in one color that drifts randomly (v1 "Rainbow" in rainbow.cpp).
//
// Every RGB channel rises or falls by its own random step, `speed` steps per
// second. At 0 or 255 the channel stops at the bound, takes a new random step
// (1..maxStep) and turns around. v1 moved one step per loop pass; here the move
// follows the elapsed time, so the drift does not depend on FPS:
//   value += direction * step * speed * deltaMs / 1000
// Values are kept in thousandths of a unit, which makes that sum exact in integers.
//   speed    steps per second; 0 = still
//   max_step largest step in color units (0..255 per channel)
// Randomness comes from FastLED random8(); App seeds it at start.
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kMaxStep };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 30},
    // Not maxStep: parameter ids are lowercase (EffectRegistry).
    {"max_step", "Max step", ParamKind::Slider, 1, 64, 5},
};

constexpr int32_t kScale = 1000;  // one color unit in channel values
constexpr int32_t kTop = 255 * kScale;
// Limits deltaMs, so a long stall cannot overflow the move. A channel crosses the
// whole range in far less time anyway.
constexpr uint32_t kMaxStepMs = 10000;

class ColorFlowEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    // As in v1: random start color, random steps, all channels rising.
    for (Channel& channel : channels_) {
      channel.value = static_cast<int32_t>(random8()) * kScale;
      channel.step = randomStep(ctx.params[kMaxStep]);
      channel.up = true;
    }
    dirty_ = true;
  }

  void onParamChanged(uint8_t index, FrameContext& ctx) override {
    if (index == kMaxStep) {
      const int32_t maxStep = ctx.params[kMaxStep];
      for (Channel& channel : channels_) {
        channel.step = channel.step > maxStep ? maxStep : channel.step;
      }
    }
  }

  bool render(FrameContext& ctx) override {
    const uint32_t deltaMs = ctx.deltaMs < kMaxStepMs ? ctx.deltaMs : kMaxStepMs;
    const int32_t speed = ctx.params[kSpeed];
    const int32_t maxStep = ctx.params[kMaxStep];
    for (Channel& channel : channels_) {
      const int32_t move = channel.step * speed * static_cast<int32_t>(deltaMs);
      if (channel.up) {
        channel.value += move;
        if (channel.value >= kTop) {
          channel.value = kTop;
          channel.step = randomStep(maxStep);
          channel.up = false;
        }
      } else {
        channel.value -= move;
        if (channel.value <= 0) {
          channel.value = 0;
          channel.step = randomStep(maxStep);
          channel.up = true;
        }
      }
    }

    const CRGB color(level(0), level(1), level(2));
    if (!dirty_ && color == shown_) {
      return false;  // The move was below one color unit.
    }
    dirty_ = false;
    shown_ = color;
    ctx.canvas.fill(color);
    return true;
  }

 private:
  struct Channel {
    int32_t value;  // 0..kTop
    int32_t step;   // 1..maxStep
    bool up;
  };

  static int32_t randomStep(int32_t maxStep) {
    return 1 + random8(static_cast<uint8_t>(maxStep));
  }

  uint8_t level(int index) const { return static_cast<uint8_t>(channels_[index].value / kScale); }

  Channel channels_[3] = {};
  CRGB shown_ = CRGB::Black;
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(ColorFlowEffect, "color-flow", "Color flow", 70, 0, kParams);

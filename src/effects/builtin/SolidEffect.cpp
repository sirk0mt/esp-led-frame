// Solid color (v1 "Static"). Draws only after a parameter change (kFlagStatic).
#include "effects/EffectRegistry.h"

namespace {

using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kColor };

constexpr ParamSpec kParams[] = {
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFFFFF},
};

class SolidEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(ctx.userColor(ctx.params[kColor]));
    return true;
  }
};

}  // namespace

REGISTER_EFFECT(SolidEffect, "solid", "Solid", 10, ledframe::kFlagStatic, kParams);

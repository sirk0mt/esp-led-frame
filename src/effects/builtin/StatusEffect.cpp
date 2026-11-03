// Hidden status effect: a red warning triangle (v1 showed one in AP mode).
// App shows it with SetOverride while the device runs its own access point.
#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;

// Apex at the top center, base across the bottom row.
void drawTriangle(Canvas& canvas, const CRGB& color) {
  const int width = canvas.width();
  const int height = canvas.height();
  canvas.clear();
  const int center2 = width - 1;  // twice the horizontal center, keeps it exact
  for (int y = 0; y < height; ++y) {
    // Half width grows from 0 at the top to the full half width at the bottom.
    int half2 = height > 1 ? (y * (width - 1)) / (height - 1) : width - 1;
    if (width % 2 == 0 && half2 < 1) {
      half2 = 1;  // An even width has two center pixels; the apex lights both.
    }
    for (int x = 0; x < width; ++x) {
      const int offset2 = 2 * x - center2;
      if (offset2 >= -half2 && offset2 <= half2) {
        canvas.at(x, y) = color;
      }
    }
  }
}

class StatusEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    drawTriangle(ctx.canvas, CRGB(255, 0, 0));
    return true;
  }
};

}  // namespace

REGISTER_EFFECT(StatusEffect, "status", "Status", 1000,
                ledframe::kFlagHidden | ledframe::kFlagStatic | ledframe::kFlag2D, nullptr);

// Picture drawn in the web UI (v1 "Paint"). Draws only when the picture or a
// parameter changes (kFlagStatic): App calls EffectEngine::redraw() after it applied
// new pixels to the shared PaintImage.
//
// The picture lives in sharedPaintImage(), not in the effect: it survives a change
// of effect and is saved to /paint.bin by App.
#include "effects/EffectRegistry.h"
#include "effects/PaintImage.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::PaintImage;

class PaintEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    // App keeps the image at the canvas size; copy the common part anyway, so a
    // layout change in between never reads outside either buffer.
    const PaintImage& image = ledframe::sharedPaintImage();
    Canvas& canvas = ctx.canvas;
    canvas.clear();
    const int width = canvas.width() < image.width() ? canvas.width() : image.width();
    const int height = canvas.height() < image.height() ? canvas.height() : image.height();
    CRGB* pixels = canvas.data();
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pixels[y * canvas.width() + x] = ctx.userColor(image.data()[y * image.width() + x]);
      }
    }
    return true;
  }
};

}  // namespace

REGISTER_EFFECT_WITH_WIDGET(PaintEffect, "paint", "Paint", 80,
                            ledframe::kFlagStatic | ledframe::kFlag2D, nullptr, "paint");

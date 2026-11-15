#include "render/SoftDraw.h"

#include <cmath>

namespace ledframe {

namespace {

inline int clampIndex(int value, int size) {
  return value < 0 ? 0 : (value >= size ? size - 1 : value);
}

}  // namespace

void drawSoftDot(Canvas& canvas, float x, float y, const CRGB& color, float radius) {
  const int width = canvas.width();
  const float radius2 = radius * radius;
  const float glowScale = 256.0f / radius2;  // d^2 to an index of kGlow
  // Through int: float to int is one instruction, float to unsigned is a call.
  const int left = clampIndex(static_cast<int>(std::floor(x - radius)), width);
  const int right = clampIndex(static_cast<int>(std::ceil(x + radius)), width);
  const int top = clampIndex(static_cast<int>(std::floor(y - radius)), canvas.height());
  const int bottom = clampIndex(static_cast<int>(std::ceil(y + radius)), canvas.height());
  for (int py = top; py <= bottom; ++py) {
    const float dy = static_cast<float>(py) - y;
    const float dy2 = dy * dy;
    CRGB* row = canvas.data() + py * width;
    for (int px = left; px <= right; ++px) {
      const float dx = static_cast<float>(px) - x;
      const float distance2 = dx * dx + dy2;
      if (distance2 >= radius2) {
        continue;
      }
      // The product may round up to 256 just below the radius.
      const int index = static_cast<int>(distance2 * glowScale);
      lighten(row[px], color, kGlow.level[index < 255 ? index : 255]);
    }
  }
}

}  // namespace ledframe

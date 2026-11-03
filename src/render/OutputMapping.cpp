#include "render/OutputMapping.h"

namespace ledframe {

const char* colorOrderName(ColorOrder order) {
  switch (order) {
    case ColorOrder::kRGB:
      return "RGB";
    case ColorOrder::kRBG:
      return "RBG";
    case ColorOrder::kGRB:
      return "GRB";
    case ColorOrder::kGBR:
      return "GBR";
    case ColorOrder::kBRG:
      return "BRG";
    case ColorOrder::kBGR:
      return "BGR";
  }
  return "?";
}

CRGB applyColorOrder(const CRGB& color, ColorOrder order) {
  switch (order) {
    case ColorOrder::kRGB:
      return color;
    case ColorOrder::kRBG:
      return CRGB(color.r, color.b, color.g);
    case ColorOrder::kGRB:
      return CRGB(color.g, color.r, color.b);
    case ColorOrder::kGBR:
      return CRGB(color.g, color.b, color.r);
    case ColorOrder::kBRG:
      return CRGB(color.b, color.r, color.g);
    case ColorOrder::kBGR:
      return CRGB(color.b, color.g, color.r);
  }
  return color;
}

void mapFrame(const Frame& frame, ColorOrder order, CRGB* physical) {
  const CRGB* logical = frame.canvas.data();
  const uint16_t* table = frame.layout.table();
  // The engine keeps both sizes equal; the minimum guards against a mismatch.
  const uint16_t count =
      frame.canvas.size() < frame.layout.size() ? frame.canvas.size() : frame.layout.size();
  for (uint16_t i = 0; i < count; ++i) {
    physical[table[i]] = applyColorOrder(logical[i], order);
  }
}

void fillTestPattern(CRGB* physical, uint16_t count) {
  for (uint16_t i = 0; i < count; ++i) {
    const uint32_t third = static_cast<uint32_t>(i) * 3 / count;  // 0, 1 or 2
    physical[i] = third == 0 ? CRGB(255, 0, 0) : third == 1 ? CRGB(0, 255, 0) : CRGB(0, 0, 255);
  }
  if (count > 0) {
    physical[0] = CRGB(255, 255, 255);
  }
}

}  // namespace ledframe

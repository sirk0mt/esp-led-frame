#include "render/Canvas.h"

#include <colorutils.h>

namespace ledframe {

bool Canvas::resize(uint16_t width, uint16_t height) {
  const uint32_t pixels = static_cast<uint32_t>(width) * height;
  if (width == 0 || height == 0 || pixels > kMaxLeds) {
    return false;
  }
  width_ = width;
  height_ = height;
  clear();
  return true;
}

CRGB& Canvas::at(int x, int y) {
  if (!contains(x, y)) {
    return scratch();
  }
  return pixels_[y * width_ + x];
}

const CRGB& Canvas::at(int x, int y) const {
  if (!contains(x, y)) {
    return scratch();
  }
  return pixels_[y * width_ + x];
}

CRGB& Canvas::operator[](int index) {
  if (!contains(index)) {
    return scratch();
  }
  return pixels_[index];
}

const CRGB& Canvas::operator[](int index) const {
  if (!contains(index)) {
    return scratch();
  }
  return pixels_[index];
}

void Canvas::fill(const CRGB& color) {
  const uint16_t count = size();
  for (uint16_t i = 0; i < count; ++i) {
    pixels_[i] = color;
  }
}

void Canvas::clear() {
  fill(CRGB::Black);
}

void Canvas::fadeToBlackBy(uint8_t amount) {
  ::fadeToBlackBy(pixels_, size(), amount);
}

CRGB& Canvas::scratch() const {
  scratch_ = CRGB::Black;
  return scratch_;
}

}  // namespace ledframe

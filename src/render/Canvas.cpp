#include "render/Canvas.h"

#include <colorutils.h>
#include <lib8tion.h>

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

namespace {

// One blur pass along a row or a column: every pixel keeps keep/256 of itself and
// passes seep/256 to each neighbor. Works on one color channel at a time, whose bytes
// lie 3 * stride apart (CRGB is r, g, b), with plain byte math: the esp32dev firmware
// is built with -Og, and CRGB copies per pixel made this loop about three times slower.
void blurLine(CRGB* first, int count, int stride, uint8_t keep, uint8_t seep) {
  const int step = 3 * stride;
  for (int channel = 0; channel < 3; ++channel) {
    uint8_t* value = &first->raw[channel];
    uint8_t carry = 0;
    for (int i = 0; i < count; ++i, value += step) {
      const uint8_t part = scale8(*value, seep);
      *value = qadd8(scale8(*value, keep), carry);
      if (i > 0) {
        value[-step] = qadd8(value[-step], part);
      }
      carry = part;
    }
  }
}

}  // namespace

void Canvas::blur(uint8_t amount, bool smear) {
  if (amount == 0) {
    return;
  }
  const uint8_t keep = smear ? 255 : static_cast<uint8_t>(255 - amount);
  const uint8_t seep = amount >> 1;
  for (int y = 0; y < height_; ++y) {
    blurLine(pixels_ + y * width_, width_, 1, keep, seep);
  }
  for (int x = 0; x < width_; ++x) {
    blurLine(pixels_ + x, height_, width_, keep, seep);
  }
}

CRGB& Canvas::scratch() const {
  scratch_ = CRGB::Black;
  return scratch_;
}

}  // namespace ledframe

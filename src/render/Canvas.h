// Logical frame buffer written by effects.
//
// Pixels are stored row by row: index = y * width + x. The buffer has a fixed
// capacity of kMaxLeds, so resizing never allocates memory.
#pragma once

#include <crgb.h>

#include <cstdint>

#include "core/Limits.h"

namespace ledframe {

class Canvas {
 public:
  Canvas() = default;

  // Changes the size and clears all pixels. Returns false and keeps the current
  // size when width or height is zero or width * height exceeds kMaxLeds.
  bool resize(uint16_t width, uint16_t height);

  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  uint16_t size() const { return static_cast<uint16_t>(width_ * height_); }

  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
  bool contains(int index) const { return index >= 0 && index < size(); }

  // Out-of-range access returns a black scratch pixel. Writes to it are discarded.
  CRGB& at(int x, int y);
  const CRGB& at(int x, int y) const;
  CRGB& operator[](int index);
  const CRGB& operator[](int index) const;

  void fill(const CRGB& color);
  void clear();
  // Dims every pixel by amount/256 of its brightness (FastLED fadeToBlackBy).
  void fadeToBlackBy(uint8_t amount);

  CRGB* data() { return pixels_; }
  const CRGB* data() const { return pixels_; }

 private:
  CRGB& scratch() const;

  uint16_t width_ = 0;
  uint16_t height_ = 0;
  CRGB pixels_[kMaxLeds];
  mutable CRGB scratch_;
};

}  // namespace ledframe

// Image of the paint effect.
//
// The image outlives the effect instance: it stays when another effect is chosen
// and is saved to /paint.bin. Pixels are stored row by row like Canvas
// (index = y * width + x) with a fixed capacity of kMaxLeds, so nothing allocates.
//
// Threading: the render task is the only writer. App applies the changes from
// the WebSocket there, under its mutex, and other tasks read the image only through
// App under the same mutex. The paint effect reads it in the render task.
//
// Binary format of /paint.bin and GET /api/paint, version 1, little-endian:
//   'L' 'P' 0x01 0x00 | width u16 | height u16 | width * height * (r, g, b)
#pragma once

#include <crgb.h>

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"
#include "core/Limits.h"

namespace ledframe {

// One change of the image.
struct PaintOp {
  enum Kind : uint8_t { kPixel, kFill, kClear };
  Kind kind = kPixel;
  uint16_t x = 0;  // kPixel only
  uint16_t y = 0;
  CRGB color;  // kPixel and kFill
};

class PaintImage {
 public:
  static constexpr size_t kHeaderSize = 8;
  static constexpr size_t kMaxBytes = kHeaderSize + kMaxLeds * 3;
  static constexpr uint8_t kFormatVersion = 1;

  // Changes the size and keeps the part that fits in both sizes; new pixels are
  // black. Returns false and keeps the image when a side is zero or the size
  // exceeds kMaxLeds.
  bool resize(uint16_t width, uint16_t height);

  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  uint16_t size() const { return static_cast<uint16_t>(width_ * height_); }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

  // Applies one change. Returns false for a pixel outside the image, which is
  // ignored. Every applied change raises version().
  bool apply(const PaintOp& op);

  // Pixel or black outside the image.
  CRGB at(int x, int y) const;
  const CRGB* data() const { return pixels_; }

  // Grows with every change, resize() and deserialize(): App saves the file when it
  // differs from the saved one.
  uint32_t version() const { return version_; }

  // Length of the binary format at the current size.
  size_t serializedSize() const { return kHeaderSize + static_cast<size_t>(size()) * 3; }
  // Writes the binary format. Returns the length, or 0 when capacity is too small.
  size_t serialize(uint8_t* out, size_t capacity) const;
  // Writes up to capacity bytes of the binary format from byte offset on. Returns
  // the number written, 0 at the end. App saves the picture in small parts this way.
  size_t serializePart(size_t offset, uint8_t* out, size_t capacity) const;
  // Reads the binary format. Returns false and keeps the image for a wrong header,
  // size or length.
  bool deserialize(const uint8_t* data, size_t length);
  // Same from a stream of `length` bytes. Data that ends early after a valid header
  // leaves an empty image (size 0).
  bool deserialize(IByteReader& in, size_t length);

 private:
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  uint32_t version_ = 0;
  CRGB pixels_[kMaxLeds];
};

// The image shared by App and the paint effect.
PaintImage& sharedPaintImage();

}  // namespace ledframe

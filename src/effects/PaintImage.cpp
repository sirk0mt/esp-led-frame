#include "effects/PaintImage.h"

#include <cstring>

namespace ledframe {

namespace {

constexpr uint8_t kMagic0 = 'L';
constexpr uint8_t kMagic1 = 'P';

void putU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t getU16(const uint8_t* in) {
  return static_cast<uint16_t>(in[0] | (in[1] << 8));
}

bool validSize(uint32_t width, uint32_t height) {
  return width > 0 && height > 0 && width * height <= kMaxLeds;
}

}  // namespace

bool PaintImage::resize(uint16_t width, uint16_t height) {
  if (!validSize(width, height)) {
    return false;
  }
  if (width == width_ && height == height_) {
    return true;
  }
  // In place: a wider image moves rows towards the end, so copy from the last row
  // back; a narrower one moves them towards the start, so copy from the first row.
  const uint16_t keepWidth = width < width_ ? width : width_;
  const uint16_t keepHeight = height < height_ ? height : height_;
  CRGB* const pixels = pixels_;
  const auto moveRow = [&](int y) {
    std::memmove(&pixels[y * width], &pixels[y * width_], keepWidth * sizeof(CRGB));
    for (int x = keepWidth; x < width; ++x) {
      pixels[y * width + x] = CRGB::Black;
    }
  };
  if (width > width_) {
    for (int y = keepHeight - 1; y >= 0; --y) {
      moveRow(y);
    }
  } else {
    for (int y = 0; y < keepHeight; ++y) {
      moveRow(y);
    }
  }
  for (int i = keepHeight * width; i < width * height; ++i) {
    pixels[i] = CRGB::Black;
  }
  width_ = width;
  height_ = height;
  ++version_;
  return true;
}

bool PaintImage::apply(const PaintOp& op) {
  switch (op.kind) {
    case PaintOp::kPixel:
      if (!contains(op.x, op.y)) {
        return false;
      }
      pixels_[op.y * width_ + op.x] = op.color;
      break;
    case PaintOp::kFill:
      for (uint16_t i = 0; i < size(); ++i) {
        pixels_[i] = op.color;
      }
      break;
    case PaintOp::kClear:
      for (uint16_t i = 0; i < size(); ++i) {
        pixels_[i] = CRGB::Black;
      }
      break;
    default:
      return false;
  }
  ++version_;
  return true;
}

CRGB PaintImage::at(int x, int y) const {
  return contains(x, y) ? pixels_[y * width_ + x] : CRGB(CRGB::Black);
}

size_t PaintImage::serialize(uint8_t* out, size_t capacity) const {
  const size_t length = serializedSize();
  if (out == nullptr || capacity < length) {
    return 0;
  }
  return serializePart(0, out, length);
}

size_t PaintImage::serializePart(size_t offset, uint8_t* out, size_t capacity) const {
  uint8_t header[kHeaderSize] = {kMagic0, kMagic1, kFormatVersion, 0};
  putU16(header + 4, width_);
  putU16(header + 6, height_);
  const size_t length = serializedSize();
  size_t count = 0;
  for (size_t position = offset; position < length && count < capacity; ++position) {
    if (position < kHeaderSize) {
      out[count++] = header[position];
      continue;
    }
    const CRGB& pixel = pixels_[(position - kHeaderSize) / 3];
    out[count++] = pixel.raw[(position - kHeaderSize) % 3];  // r, g, b
  }
  return count;
}

bool PaintImage::deserialize(const uint8_t* data, size_t length) {
  ArrayReader reader(reinterpret_cast<const char*>(data), length);
  return deserialize(reader, length);
}

bool PaintImage::deserialize(IByteReader& in, size_t length) {
  uint8_t header[kHeaderSize];
  if (in.readBytes(reinterpret_cast<char*>(header), kHeaderSize) != kHeaderSize ||
      header[0] != kMagic0 || header[1] != kMagic1 || header[2] != kFormatVersion) {
    return false;
  }
  const uint16_t width = getU16(header + 4);
  const uint16_t height = getU16(header + 6);
  if (!validSize(width, height) ||
      length != kHeaderSize + static_cast<size_t>(width) * height * 3) {
    return false;
  }
  width_ = width;
  height_ = height;
  ++version_;
  uint8_t rgb[3];
  for (uint16_t i = 0; i < size(); ++i) {
    if (in.readBytes(reinterpret_cast<char*>(rgb), sizeof(rgb)) != sizeof(rgb)) {
      width_ = 0;  // The pixels read so far mix with old ones: drop the picture.
      height_ = 0;
      return false;
    }
    pixels_[i] = CRGB(rgb[0], rgb[1], rgb[2]);
  }
  return true;
}

PaintImage& sharedPaintImage() {
  static PaintImage image;
  return image;
}

}  // namespace ledframe

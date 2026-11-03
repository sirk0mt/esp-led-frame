#include "net/WsProtocol.h"

#include <cstring>

namespace ledframe {

bool isPing(const uint8_t* data, size_t length) {
  return data != nullptr && length == 4 && std::memcmp(data, "ping", 4) == 0;
}

BinaryKind classifyBinary(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0) {
    return BinaryKind::kEmpty;
  }
  switch (static_cast<BinaryType>(data[0])) {
    case BinaryType::kPaintPixels:
      return BinaryKind::kPaintPixels;
    case BinaryType::kPreview:
      return BinaryKind::kPreview;
  }
  return BinaryKind::kUnknown;
}

PaintParse parsePaintMessage(const uint8_t* data, size_t length, uint16_t width, uint16_t height,
                             PaintOp* ops, size_t capacity) {
  PaintParse result;
  if (data == nullptr || length < 2 || data[0] != static_cast<uint8_t>(BinaryType::kPaintPixels)) {
    result.error = "paint message too short";
    return result;
  }
  const uint8_t* body = data + 2;
  const size_t bodyLength = length - 2;
  switch (static_cast<PaintCommand>(data[1])) {
    case PaintCommand::kClear:
      if (bodyLength != 0 || capacity < 1) {
        result.error = "paint clear takes no data";
        return result;
      }
      ops[0].kind = PaintOp::kClear;
      result.count = 1;
      return result;
    case PaintCommand::kFill:
      if (bodyLength != 3 || capacity < 1) {
        result.error = "paint fill takes r, g, b";
        return result;
      }
      ops[0].kind = PaintOp::kFill;
      ops[0].color = CRGB(body[0], body[1], body[2]);
      result.count = 1;
      return result;
    case PaintCommand::kPixels: {
      if (bodyLength == 0 || bodyLength % kPaintPixelBytes != 0) {
        result.error = "paint pixels take 7 bytes each";
        return result;
      }
      const size_t pixels = bodyLength / kPaintPixelBytes;
      if (pixels > kPaintMaxPixels || pixels > capacity) {
        result.error = "too many paint pixels";
        return result;
      }
      for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* p = body + i * kPaintPixelBytes;
        const uint16_t x = static_cast<uint16_t>(p[0] | (p[1] << 8));
        const uint16_t y = static_cast<uint16_t>(p[2] | (p[3] << 8));
        if (x >= width || y >= height) {
          ++result.outside;
          continue;
        }
        PaintOp& op = ops[result.count++];
        op.kind = PaintOp::kPixel;
        op.x = x;
        op.y = y;
        op.color = CRGB(p[4], p[5], p[6]);
      }
      return result;
    }
  }
  result.error = "unknown paint command";
  return result;
}

PreviewRequest parsePreviewRequest(const uint8_t* data, size_t length) {
  if (data == nullptr || length != 2 || data[0] != static_cast<uint8_t>(BinaryType::kPreview)) {
    return PreviewRequest::kInvalid;
  }
  switch (static_cast<PreviewCommand>(data[1])) {
    case PreviewCommand::kStart:
      return PreviewRequest::kStart;
    case PreviewCommand::kStop:
      return PreviewRequest::kStop;
    case PreviewCommand::kFrame:
      break;
  }
  return PreviewRequest::kInvalid;
}

size_t writePreviewFrame(const CRGB* pixels, uint16_t width, uint16_t height, uint8_t* out,
                         size_t capacity) {
  const size_t count = static_cast<size_t>(width) * height;
  const size_t length = kPreviewHeaderSize + count * 3;
  if (pixels == nullptr || out == nullptr || count > kMaxLeds || capacity < length) {
    return 0;
  }
  out[0] = static_cast<uint8_t>(BinaryType::kPreview);
  out[1] = static_cast<uint8_t>(PreviewCommand::kFrame);
  out[2] = static_cast<uint8_t>(width & 0xFF);
  out[3] = static_cast<uint8_t>(width >> 8);
  out[4] = static_cast<uint8_t>(height & 0xFF);
  out[5] = static_cast<uint8_t>(height >> 8);
  uint8_t* p = out + kPreviewHeaderSize;
  for (size_t i = 0; i < count; ++i) {
    *p++ = pixels[i].r;
    *p++ = pixels[i].g;
    *p++ = pixels[i].b;
  }
  return length;
}

bool BroadcastThrottle::shouldSend(uint32_t version, uint32_t nowMs) const {
  if (sentOnce_ && version == lastVersion_) {
    return false;
  }
  return !sentOnce_ || nowMs - lastSentMs_ >= kMinIntervalMs;
}

void BroadcastThrottle::markSent(uint32_t version, uint32_t nowMs) {
  sentOnce_ = true;
  lastVersion_ = version;
  lastSentMs_ = nowMs;
}

}  // namespace ledframe

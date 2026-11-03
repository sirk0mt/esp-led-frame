// WebSocket protocol helpers (plain logic, tested natively).
//
// Text messages: the same partial state changes as POST /api/state. The server
// broadcasts the state JSON of GET /api/state after every state change. The text
// "ping" (not JSON) gets the reply "pong" to the sender only: browsers cannot send
// WebSocket ping frames, and the UI uses it to find a dead connection.
// Binary messages: the first byte selects the message type: paint changes
// (parsePaintMessage below) and the live preview (PreviewRequest below).
#pragma once

#include <cstddef>
#include <cstdint>

#include "effects/PaintImage.h"

namespace ledframe {

// First byte of a binary message.
enum class BinaryType : uint8_t {
  kPaintPixels = 0x01,  // client -> device: pixels for the paint effect
  kPreview = 0x02,      // device -> client frames; client -> device subscribe/unsubscribe
};

enum class BinaryKind : uint8_t { kEmpty, kPaintPixels, kPreview, kUnknown };

BinaryKind classifyBinary(const uint8_t* data, size_t length);

// Paint message (first byte 0x01). The second byte is the command:
//   0x00 pixels: then per pixel x u16 LE, y u16 LE, r, g, b (7 bytes)
//   0x01 clear
//   0x02 fill: then r, g, b
enum class PaintCommand : uint8_t { kPixels = 0x00, kClear = 0x01, kFill = 0x02 };

inline constexpr size_t kPaintPixelBytes = 7;
// The largest message carries this many pixels (2048 B, the text message limit).
inline constexpr size_t kPaintMaxPixels = (2048 - 2) / kPaintPixelBytes;

struct PaintParse {
  size_t count = 0;            // ops written
  size_t outside = 0;          // pixels outside the image, dropped
  const char* error = nullptr;  // set when the whole message is rejected
};

// Turns a paint message into ops for an image of width x height. Pixels outside the
// image are dropped and counted; the rest are applied. A malformed message gives
// error and no ops. ops must hold kPaintMaxPixels entries.
PaintParse parsePaintMessage(const uint8_t* data, size_t length, uint16_t width, uint16_t height,
                             PaintOp* ops, size_t capacity);

// Live preview (first byte 0x02). Client -> device: 0x02 0x01 starts, 0x02 0x00
// stops. Device -> client: 0x02 0x10, width u16 LE, height u16 LE, then the
// logical canvas row by row, r, g, b per pixel (black while the power is off).
enum class PreviewCommand : uint8_t { kStop = 0x00, kStart = 0x01, kFrame = 0x10 };
enum class PreviewRequest : uint8_t { kInvalid, kStop, kStart };

inline constexpr size_t kPreviewHeaderSize = 6;
inline constexpr size_t kPreviewMaxBytes = kPreviewHeaderSize + kMaxLeds * 3;

PreviewRequest parsePreviewRequest(const uint8_t* data, size_t length);

// Writes a preview frame. Returns the length, or 0 when capacity is too small.
size_t writePreviewFrame(const CRGB* pixels, uint16_t width, uint16_t height, uint8_t* out,
                         size_t capacity);

inline constexpr char kPongText[] = "pong";

// True for the text message "ping" exactly.
bool isPing(const uint8_t* data, size_t length);

// Limits state broadcasts to changes of the snapshot version, at most one per
// kMinIntervalMs. A change during the pause is sent when the pause ends, so the
// last state always reaches the clients.
class BroadcastThrottle {
 public:
  static constexpr uint32_t kMinIntervalMs = 50;

  bool shouldSend(uint32_t version, uint32_t nowMs) const;
  void markSent(uint32_t version, uint32_t nowMs);

 private:
  bool sentOnce_ = false;
  uint32_t lastVersion_ = 0;
  uint32_t lastSentMs_ = 0;
};

}  // namespace ledframe

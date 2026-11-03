#include <unity.h>

#include <vector>

#include "net/WsProtocol.h"

using ledframe::BinaryKind;
using ledframe::BroadcastThrottle;

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

void setUp() {}
void tearDown() {}

void test_first_state_is_sent_right_away() {
  BroadcastThrottle throttle;
  TEST_ASSERT_TRUE(throttle.shouldSend(1, 0));
  TEST_ASSERT_TRUE(throttle.shouldSend(0, 12345));
}

void test_same_version_is_not_sent_again() {
  BroadcastThrottle throttle;
  throttle.markSent(5, 100);
  TEST_ASSERT_FALSE(throttle.shouldSend(5, 200));
  TEST_ASSERT_FALSE(throttle.shouldSend(5, 100000));
}

void test_changes_wait_for_the_interval() {
  BroadcastThrottle throttle;
  throttle.markSent(5, 1000);
  TEST_ASSERT_FALSE(throttle.shouldSend(6, 1010));
  TEST_ASSERT_FALSE(throttle.shouldSend(7, 1049));
  // The pause ends: the latest version goes out, intermediate ones are skipped.
  TEST_ASSERT_TRUE(throttle.shouldSend(8, 1050));
  throttle.markSent(8, 1050);
  TEST_ASSERT_FALSE(throttle.shouldSend(8, 1200));
  TEST_ASSERT_TRUE(throttle.shouldSend(9, 1200));
}

void test_interval_survives_timer_wrap() {
  BroadcastThrottle throttle;
  throttle.markSent(1, 0xFFFFFFF0u);
  TEST_ASSERT_FALSE(throttle.shouldSend(2, 0x00000010u));  // 32 ms later
  TEST_ASSERT_TRUE(throttle.shouldSend(2, 0x00000022u));   // 50 ms later
}

void test_binary_message_types() {
  const uint8_t paint[] = {0x01, 0, 0, 0};
  const uint8_t preview[] = {0x02};
  const uint8_t unknown[] = {0x7F, 1};
  ASSERT_ENUM(BinaryKind::kPaintPixels, ledframe::classifyBinary(paint, sizeof(paint)));
  ASSERT_ENUM(BinaryKind::kPreview, ledframe::classifyBinary(preview, sizeof(preview)));
  ASSERT_ENUM(BinaryKind::kUnknown, ledframe::classifyBinary(unknown, sizeof(unknown)));
  ASSERT_ENUM(BinaryKind::kEmpty, ledframe::classifyBinary(paint, 0));
  ASSERT_ENUM(BinaryKind::kEmpty, ledframe::classifyBinary(nullptr, 3));
}

void test_ping_is_exact_text() {
  const auto bytes = [](const char* text) { return reinterpret_cast<const uint8_t*>(text); };
  TEST_ASSERT_TRUE(ledframe::isPing(bytes("ping"), 4));
  TEST_ASSERT_FALSE(ledframe::isPing(bytes("ping "), 5));
  TEST_ASSERT_FALSE(ledframe::isPing(bytes("pin"), 3));
  TEST_ASSERT_FALSE(ledframe::isPing(bytes("PING"), 4));
  TEST_ASSERT_FALSE(ledframe::isPing(bytes("{\"ping\":1}"), 10));
  TEST_ASSERT_FALSE(ledframe::isPing(nullptr, 4));
  TEST_ASSERT_EQUAL_STRING("pong", ledframe::kPongText);
}

// --- Paint messages ---------------------------------------------------------------

using ledframe::PaintOp;
using ledframe::PaintParse;

namespace {

PaintOp ops[ledframe::kPaintMaxPixels];

// Appends one pixel record: x and y little-endian, then r, g, b.
void addPixel(std::vector<uint8_t>& message, uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t bytes[] = {static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>(x >> 8),
                           static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>(y >> 8), r, g, b};
  message.insert(message.end(), bytes, bytes + sizeof(bytes));
}

PaintParse parse(const std::vector<uint8_t>& message, uint16_t width = 15, uint16_t height = 10) {
  return ledframe::parsePaintMessage(message.data(), message.size(), width, height, ops,
                                     ledframe::kPaintMaxPixels);
}

}  // namespace

void test_paint_pixels_are_parsed() {
  std::vector<uint8_t> message = {0x01, 0x00};
  addPixel(message, 0, 0, 255, 0, 0);
  addPixel(message, 14, 9, 1, 2, 3);
  addPixel(message, 0x0102, 0, 9, 9, 9);  // x = 258: both bytes count
  const PaintParse result = parse(message, 300, 10);
  TEST_ASSERT_NULL(result.error);
  TEST_ASSERT_EQUAL_UINT32(3, result.count);
  TEST_ASSERT_EQUAL_UINT32(0, result.outside);
  TEST_ASSERT_EQUAL_INT(PaintOp::kPixel, ops[1].kind);
  TEST_ASSERT_EQUAL_UINT16(14, ops[1].x);
  TEST_ASSERT_EQUAL_UINT16(9, ops[1].y);
  TEST_ASSERT_EQUAL_UINT8(1, ops[1].color.r);
  TEST_ASSERT_EQUAL_UINT8(3, ops[1].color.b);
  TEST_ASSERT_EQUAL_UINT16(258, ops[2].x);  // little-endian x
}

void test_paint_pixels_outside_are_dropped() {
  std::vector<uint8_t> message = {0x01, 0x00};
  addPixel(message, 15, 0, 1, 1, 1);  // x == width
  addPixel(message, 3, 4, 2, 2, 2);
  addPixel(message, 0, 10, 3, 3, 3);  // y == height
  addPixel(message, 0xFFFF, 0xFFFF, 4, 4, 4);
  const PaintParse result = parse(message);
  TEST_ASSERT_NULL(result.error);
  TEST_ASSERT_EQUAL_UINT32(1, result.count);
  TEST_ASSERT_EQUAL_UINT32(3, result.outside);
  TEST_ASSERT_EQUAL_UINT16(3, ops[0].x);
  TEST_ASSERT_EQUAL_UINT16(4, ops[0].y);
}

void test_paint_clear_and_fill() {
  PaintParse result = parse({0x01, 0x01});
  TEST_ASSERT_NULL(result.error);
  TEST_ASSERT_EQUAL_UINT32(1, result.count);
  TEST_ASSERT_EQUAL_INT(PaintOp::kClear, ops[0].kind);
  result = parse({0x01, 0x02, 10, 20, 30});
  TEST_ASSERT_NULL(result.error);
  TEST_ASSERT_EQUAL_INT(PaintOp::kFill, ops[0].kind);
  TEST_ASSERT_EQUAL_UINT8(20, ops[0].color.g);
}

void test_paint_malformed_messages_are_rejected() {
  TEST_ASSERT_NOT_NULL(parse({0x01}).error);                     // no command
  TEST_ASSERT_NOT_NULL(parse({0x01, 0x00}).error);               // no pixels
  TEST_ASSERT_NOT_NULL(parse({0x01, 0x00, 1, 0, 1, 0, 9}).error);  // 5 of 7 bytes
  TEST_ASSERT_NOT_NULL(parse({0x01, 0x01, 0}).error);            // clear with data
  TEST_ASSERT_NOT_NULL(parse({0x01, 0x02, 1, 2}).error);         // fill without blue
  TEST_ASSERT_NOT_NULL(parse({0x01, 0x07}).error);               // unknown command
  TEST_ASSERT_NOT_NULL(parse({0x02, 0x00}).error);               // not a paint message
  TEST_ASSERT_NOT_NULL(ledframe::parsePaintMessage(nullptr, 9, 15, 10, ops, 1).error);
  // One pixel over the limit.
  std::vector<uint8_t> message = {0x01, 0x00};
  for (size_t i = 0; i <= ledframe::kPaintMaxPixels; ++i) {
    addPixel(message, 0, 0, 1, 1, 1);
  }
  const PaintParse result = parse(message);
  TEST_ASSERT_NOT_NULL(result.error);
  TEST_ASSERT_EQUAL_UINT32(0, result.count);
  // The largest allowed message fits in the 2048 B message limit.
  TEST_ASSERT_TRUE(2 + ledframe::kPaintMaxPixels * ledframe::kPaintPixelBytes <= 2048);
}

// --- Live preview ---------------------------------------------------------------

void test_preview_requests() {
  using ledframe::PreviewRequest;
  const uint8_t start[] = {0x02, 0x01};
  const uint8_t stop[] = {0x02, 0x00};
  const uint8_t frame[] = {0x02, 0x10};
  const uint8_t longer[] = {0x02, 0x01, 0x00};
  const uint8_t paint[] = {0x01, 0x01};
  ASSERT_ENUM(PreviewRequest::kStart, ledframe::parsePreviewRequest(start, 2));
  ASSERT_ENUM(PreviewRequest::kStop, ledframe::parsePreviewRequest(stop, 2));
  ASSERT_ENUM(PreviewRequest::kInvalid, ledframe::parsePreviewRequest(frame, 2));
  ASSERT_ENUM(PreviewRequest::kInvalid, ledframe::parsePreviewRequest(longer, 3));
  ASSERT_ENUM(PreviewRequest::kInvalid, ledframe::parsePreviewRequest(start, 1));
  ASSERT_ENUM(PreviewRequest::kInvalid, ledframe::parsePreviewRequest(paint, 2));
  ASSERT_ENUM(PreviewRequest::kInvalid, ledframe::parsePreviewRequest(nullptr, 2));
}

void test_preview_frame_format() {
  const CRGB pixels[] = {CRGB(1, 2, 3), CRGB(4, 5, 6), CRGB(7, 8, 9),
                         CRGB(10, 11, 12), CRGB(13, 14, 15), CRGB(255, 0, 128)};
  uint8_t out[64];
  const size_t length = ledframe::writePreviewFrame(pixels, 3, 2, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(6 + 6 * 3, length);
  const uint8_t header[] = {0x02, 0x10, 3, 0, 2, 0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(header, out, sizeof(header));
  TEST_ASSERT_EQUAL_UINT8(1, out[6]);
  TEST_ASSERT_EQUAL_UINT8(9, out[6 + 2 * 3 + 2]);    // third pixel blue
  TEST_ASSERT_EQUAL_UINT8(255, out[6 + 5 * 3]);      // last pixel red
  TEST_ASSERT_EQUAL_UINT8(128, out[6 + 5 * 3 + 2]);  // last pixel blue
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writePreviewFrame(pixels, 3, 2, out, length - 1));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::writePreviewFrame(nullptr, 3, 2, out, sizeof(out)));
  // A wide layout needs both bytes of the width.
  static CRGB strip[ledframe::kMaxLeds];
  static uint8_t big[ledframe::kPreviewMaxBytes];
  TEST_ASSERT_EQUAL_UINT32(ledframe::kPreviewMaxBytes,
                           ledframe::writePreviewFrame(strip, ledframe::kMaxLeds, 1, big, sizeof(big)));
  TEST_ASSERT_EQUAL_UINT8(ledframe::kMaxLeds & 0xFF, big[2]);
  TEST_ASSERT_EQUAL_UINT8(ledframe::kMaxLeds >> 8, big[3]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_state_is_sent_right_away);
  RUN_TEST(test_same_version_is_not_sent_again);
  RUN_TEST(test_changes_wait_for_the_interval);
  RUN_TEST(test_interval_survives_timer_wrap);
  RUN_TEST(test_binary_message_types);
  RUN_TEST(test_ping_is_exact_text);
  RUN_TEST(test_paint_pixels_are_parsed);
  RUN_TEST(test_paint_pixels_outside_are_dropped);
  RUN_TEST(test_paint_clear_and_fill);
  RUN_TEST(test_paint_malformed_messages_are_rejected);
  RUN_TEST(test_preview_requests);
  RUN_TEST(test_preview_frame_format);
  return UNITY_END();
}

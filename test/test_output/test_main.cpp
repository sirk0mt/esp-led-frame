#include <unity.h>

#include "render/OutputMapping.h"

using ledframe::applyColorOrder;
using ledframe::Canvas;
using ledframe::ColorOrder;
using ledframe::Frame;
using ledframe::Layout;
using ledframe::LayoutConfig;

namespace {

uint32_t packed(const CRGB& color) {
  return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

void expectColor(uint32_t expected, const CRGB& actual) {
  TEST_ASSERT_EQUAL_HEX32(expected, packed(actual));
}

Canvas canvas;
Layout layout;
CRGB physical[16];

}  // namespace

void setUp() {
  for (CRGB& led : physical) {
    led = CRGB(1, 2, 3);
  }
}
void tearDown() {}

// Logical color 0x112233: r = 0x11, g = 0x22, b = 0x33. Byte 0 of the result is
// sent first, so for GRB the strip must receive 0x22, 0x11, 0x33.
void test_permutation_for_every_order() {
  const CRGB color(0x11, 0x22, 0x33);
  expectColor(0x112233, applyColorOrder(color, ColorOrder::kRGB));
  expectColor(0x113322, applyColorOrder(color, ColorOrder::kRBG));
  expectColor(0x221133, applyColorOrder(color, ColorOrder::kGRB));
  expectColor(0x223311, applyColorOrder(color, ColorOrder::kGBR));
  expectColor(0x331122, applyColorOrder(color, ColorOrder::kBRG));
  expectColor(0x332211, applyColorOrder(color, ColorOrder::kBGR));
}

void test_numbering_matches_v1() {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(ColorOrder::kRGB));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(ColorOrder::kRBG));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(ColorOrder::kGRB));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(ColorOrder::kGBR));
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(ColorOrder::kBRG));
  TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(ColorOrder::kBGR));
  // RGB was verified on the author's frame.
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(ledframe::kDefaultColorOrder));
}

void test_valid_orders_and_names() {
  const char* names[] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};
  for (uint8_t i = 0; i < ledframe::kColorOrderCount; ++i) {
    TEST_ASSERT_TRUE(ledframe::isValidColorOrder(i));
    TEST_ASSERT_EQUAL_STRING(names[i], ledframe::colorOrderName(static_cast<ColorOrder>(i)));
  }
  TEST_ASSERT_FALSE(ledframe::isValidColorOrder(6));
  TEST_ASSERT_FALSE(ledframe::isValidColorOrder(255));
  TEST_ASSERT_EQUAL_STRING("?", ledframe::colorOrderName(static_cast<ColorOrder>(9)));
}

void test_every_order_is_a_permutation() {
  // Pure channels must stay pure and reach a different byte for each order.
  for (uint8_t i = 0; i < ledframe::kColorOrderCount; ++i) {
    const ColorOrder order = static_cast<ColorOrder>(i);
    const CRGB r = applyColorOrder(CRGB(255, 0, 0), order);
    const CRGB g = applyColorOrder(CRGB(0, 255, 0), order);
    const CRGB b = applyColorOrder(CRGB(0, 0, 255), order);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, packed(r) | packed(g) | packed(b));
    TEST_ASSERT_EQUAL_HEX32(0, packed(r) & packed(g));
    TEST_ASSERT_EQUAL_HEX32(0, packed(g) & packed(b));
    TEST_ASSERT_EQUAL_HEX32(0, packed(r) & packed(b));
  }
}

void test_map_frame_uses_layout_and_order() {
  // 3x2 serpentine: logical A B C / D E F  ->  physical A B C F E D.
  LayoutConfig config;
  config.width = 3;
  config.height = 2;
  config.serpentine = true;
  TEST_ASSERT_TRUE(layout.configure(config));
  TEST_ASSERT_TRUE(canvas.resize(3, 2));
  for (int i = 0; i < 6; ++i) {
    canvas[i] = CRGB(static_cast<uint8_t>(i + 1), 0x10, 0x20);  // red channel = position
  }
  const Frame frame{canvas, layout, 255};

  ledframe::mapFrame(frame, ColorOrder::kRGB, physical);
  const uint8_t expectedRed[] = {1, 2, 3, 6, 5, 4};
  for (int i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(expectedRed[i], physical[i].r);
  }
  expectColor(0x010203, physical[6]);  // Past the frame: untouched.

  ledframe::mapFrame(frame, ColorOrder::kGRB, physical);
  expectColor(0x100120, physical[0]);
  expectColor(0x100620, physical[3]);
}

void test_test_pattern() {
  ledframe::fillTestPattern(physical, 9);
  expectColor(0xFFFFFF, physical[0]);
  expectColor(0xFF0000, physical[1]);
  expectColor(0xFF0000, physical[2]);
  expectColor(0x00FF00, physical[3]);
  expectColor(0x00FF00, physical[5]);
  expectColor(0x0000FF, physical[6]);
  expectColor(0x0000FF, physical[8]);
  expectColor(0x010203, physical[9]);  // Untouched.
}

void test_test_pattern_small_counts() {
  ledframe::fillTestPattern(physical, 0);
  expectColor(0x010203, physical[0]);
  ledframe::fillTestPattern(physical, 1);
  expectColor(0xFFFFFF, physical[0]);
  ledframe::fillTestPattern(physical, 3);
  expectColor(0xFFFFFF, physical[0]);
  expectColor(0x00FF00, physical[1]);
  expectColor(0x0000FF, physical[2]);
  // 16 LEDs: thirds of 6, 5 and 5 pixels.
  ledframe::fillTestPattern(physical, 16);
  expectColor(0xFF0000, physical[5]);
  expectColor(0x00FF00, physical[6]);
  expectColor(0x00FF00, physical[10]);
  expectColor(0x0000FF, physical[11]);
  expectColor(0x0000FF, physical[15]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_permutation_for_every_order);
  RUN_TEST(test_numbering_matches_v1);
  RUN_TEST(test_valid_orders_and_names);
  RUN_TEST(test_every_order_is_a_permutation);
  RUN_TEST(test_map_frame_uses_layout_and_order);
  RUN_TEST(test_test_pattern);
  RUN_TEST(test_test_pattern_small_counts);
  return UNITY_END();
}

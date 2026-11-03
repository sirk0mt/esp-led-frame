#include <unity.h>

#include "render/Canvas.h"

using ledframe::Canvas;

namespace {

Canvas canvas;  // Global: the pixel buffer is too large to place on the stack many times.

void expectColor(const CRGB& expected, const CRGB& actual) {
  TEST_ASSERT_EQUAL_UINT8(expected.r, actual.r);
  TEST_ASSERT_EQUAL_UINT8(expected.g, actual.g);
  TEST_ASSERT_EQUAL_UINT8(expected.b, actual.b);
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(3, 2));
}
void tearDown() {}

void test_default_canvas_is_empty() {
  Canvas fresh;
  TEST_ASSERT_EQUAL_UINT16(0, fresh.size());
  fresh.at(0, 0) = CRGB::Red;  // Ignored: no pixels yet.
  expectColor(CRGB::Black, fresh.at(0, 0));
}

void test_resize_limits() {
  TEST_ASSERT_FALSE(canvas.resize(0, 5));
  TEST_ASSERT_FALSE(canvas.resize(5, 0));
  TEST_ASSERT_FALSE(canvas.resize(ledframe::kMaxLeds + 1, 1));
  TEST_ASSERT_FALSE(canvas.resize(0xFFFF, 0xFFFF));
  TEST_ASSERT_EQUAL_UINT16(3, canvas.width());
  TEST_ASSERT_EQUAL_UINT16(2, canvas.height());
  TEST_ASSERT_TRUE(canvas.resize(ledframe::kMaxLeds, 1));
  TEST_ASSERT_EQUAL_UINT16(ledframe::kMaxLeds, canvas.size());
}

void test_resize_clears_pixels() {
  canvas.fill(CRGB::White);
  TEST_ASSERT_TRUE(canvas.resize(2, 3));
  for (int i = 0; i < canvas.size(); ++i) {
    expectColor(CRGB::Black, canvas[i]);
  }
}

void test_at_uses_row_major_order() {
  canvas.at(2, 1) = CRGB::Blue;
  canvas.at(1, 0) = CRGB::Green;
  expectColor(CRGB::Blue, canvas[5]);
  expectColor(CRGB::Green, canvas[1]);
  expectColor(CRGB::Blue, canvas.data()[5]);
}

void test_out_of_range_writes_are_ignored() {
  canvas.fill(CRGB::Red);
  canvas.at(-1, 0) = CRGB::Blue;
  canvas.at(0, -1) = CRGB::Blue;
  canvas.at(3, 0) = CRGB::Blue;
  canvas.at(0, 2) = CRGB::Blue;
  canvas[-1] = CRGB::Blue;
  canvas[6] = CRGB::Blue;
  for (int i = 0; i < canvas.size(); ++i) {
    expectColor(CRGB::Red, canvas[i]);
  }
  // A previous out-of-range write never leaks into later out-of-range reads.
  expectColor(CRGB::Black, canvas.at(10, 10));
  expectColor(CRGB::Black, canvas[100]);
  const Canvas& view = canvas;
  expectColor(CRGB::Black, view.at(-5, 0));
  expectColor(CRGB::Black, view[-5]);
  TEST_ASSERT_FALSE(canvas.contains(3, 0));
  TEST_ASSERT_TRUE(canvas.contains(2, 1));
  TEST_ASSERT_FALSE(canvas.contains(6));
}

void test_fill_and_clear() {
  canvas.fill(CRGB(10, 20, 30));
  for (int i = 0; i < canvas.size(); ++i) {
    expectColor(CRGB(10, 20, 30), canvas[i]);
  }
  canvas.clear();
  for (int i = 0; i < canvas.size(); ++i) {
    expectColor(CRGB::Black, canvas[i]);
  }
}

void test_fill_stays_within_size() {
  TEST_ASSERT_TRUE(canvas.resize(4, 4));
  canvas.fill(CRGB::Red);
  TEST_ASSERT_TRUE(canvas.resize(2, 2));
  canvas.fill(CRGB::Green);
  // Pixels past the current size are untouched by fill() and clear().
  expectColor(CRGB::Green, canvas.data()[3]);
  expectColor(CRGB::Red, canvas.data()[4]);
}

void test_fade_to_black() {
  canvas.fill(CRGB(200, 100, 50));
  canvas.fadeToBlackBy(0);
  expectColor(CRGB(200, 100, 50), canvas[0]);

  canvas.fadeToBlackBy(128);
  CRGB expected(200, 100, 50);
  expected.nscale8(127);
  for (int i = 0; i < canvas.size(); ++i) {
    expectColor(expected, canvas[i]);
  }
  TEST_ASSERT_TRUE(canvas[0].r < 200);

  canvas.fadeToBlackBy(255);
  canvas.fadeToBlackBy(255);
  expectColor(CRGB::Black, canvas[5]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_canvas_is_empty);
  RUN_TEST(test_resize_limits);
  RUN_TEST(test_resize_clears_pixels);
  RUN_TEST(test_at_uses_row_major_order);
  RUN_TEST(test_out_of_range_writes_are_ignored);
  RUN_TEST(test_fill_and_clear);
  RUN_TEST(test_fill_stays_within_size);
  RUN_TEST(test_fade_to_black);
  return UNITY_END();
}

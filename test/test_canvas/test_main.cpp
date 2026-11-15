#include <unity.h>

#include "render/Canvas.h"
#include "render/SoftDraw.h"

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

void test_blur_spreads_light_to_neighbors() {
  TEST_ASSERT_TRUE(canvas.resize(5, 3));
  canvas.at(2, 1) = CRGB(200, 0, 0);
  canvas.blur(0);
  expectColor(CRGB(200, 0, 0), canvas.at(2, 1));
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(1, 1).r);

  canvas.blur(64);
  const int center = canvas.at(2, 1).r;
  TEST_ASSERT_TRUE(center < 200);
  TEST_ASSERT_TRUE(canvas.at(1, 1).r > 0);  // row neighbor
  TEST_ASSERT_TRUE(canvas.at(2, 0).r > 0);  // column neighbor
  TEST_ASSERT_TRUE(canvas.at(1, 0).r > 0);  // diagonal, from both passes
  TEST_ASSERT_TRUE(canvas.at(1, 0).r < canvas.at(1, 1).r);
  TEST_ASSERT_TRUE(canvas.at(1, 1).r < center);
  TEST_ASSERT_EQUAL_UINT8(canvas.at(1, 1).r, canvas.at(3, 1).r);  // symmetric
  TEST_ASSERT_EQUAL_UINT8(canvas.at(2, 0).r, canvas.at(2, 2).r);
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(0, 1).r);  // one pass reaches one pixel
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(2, 1).g);  // channels stay apart
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(1, 1).b);
}

void test_blur_with_smear_keeps_the_pixel() {
  TEST_ASSERT_TRUE(canvas.resize(5, 3));
  canvas.at(2, 1) = CRGB(200, 100, 0);
  canvas.blur(64, true);
  expectColor(CRGB(200, 100, 0), canvas.at(2, 1));
  TEST_ASSERT_TRUE(canvas.at(1, 1).r > 0);
  TEST_ASSERT_TRUE(canvas.at(1, 1).r > canvas.at(1, 1).g);
}

void test_blur_stays_within_size() {
  TEST_ASSERT_TRUE(canvas.resize(4, 2));
  canvas.data()[8] = CRGB::Blue;  // just past the canvas
  canvas.fill(CRGB::White);
  canvas.blur(200, true);
  expectColor(CRGB::Blue, canvas.data()[8]);
  expectColor(CRGB::White, canvas.at(3, 1));  // full pixels stay full when smeared
}

void test_soft_dot_is_full_at_the_point() {
  TEST_ASSERT_TRUE(canvas.resize(5, 5));
  ledframe::drawSoftDot(canvas, 2.0f, 2.0f, CRGB(200, 100, 0), 1.2f);
  expectColor(CRGB(200, 100, 0), canvas.at(2, 2));
  const CRGB side = canvas.at(1, 2);
  TEST_ASSERT_TRUE(side.r > 0 && side.r < 100);  // faint edge at 1 pixel of 1.2
  expectColor(side, canvas.at(3, 2));            // symmetric
  expectColor(side, canvas.at(2, 1));
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(1, 1).r);  // the diagonal is beyond the radius
  TEST_ASSERT_EQUAL_UINT8(0, canvas.at(0, 2).r);
}

void test_soft_dot_between_pixels_lights_both() {
  TEST_ASSERT_TRUE(canvas.resize(5, 5));
  ledframe::drawSoftDot(canvas, 1.5f, 2.0f, CRGB(0, 0, 200), 1.0f);
  TEST_ASSERT_TRUE(canvas.at(1, 2).b > 0);
  expectColor(canvas.at(1, 2), canvas.at(2, 2));
  TEST_ASSERT_TRUE(canvas.at(1, 2).b < 200);
}

void test_soft_dot_lightens_and_stays_within_the_canvas() {
  TEST_ASSERT_TRUE(canvas.resize(3, 2));
  canvas.data()[6] = CRGB::Blue;  // just past the canvas
  canvas.at(0, 0) = CRGB(0, 255, 0);
  ledframe::drawSoftDot(canvas, -0.5f, -0.5f, CRGB(255, 0, 0), 2.0f);  // mostly outside
  TEST_ASSERT_TRUE(canvas.at(0, 0).r > 0);
  TEST_ASSERT_EQUAL_UINT8(255, canvas.at(0, 0).g);  // lightening never darkens
  expectColor(CRGB::Blue, canvas.data()[6]);
  ledframe::drawSoftDot(canvas, 50.0f, -20.0f, CRGB::White, 1.5f);  // far outside
  expectColor(CRGB::Blue, canvas.data()[6]);
}

void test_glow_level_falls_off_to_zero() {
  TEST_ASSERT_EQUAL_UINT8(255, ledframe::glowLevel(0));
  TEST_ASSERT_TRUE(ledframe::glowLevel(128) < 128);  // half the radius squared
  TEST_ASSERT_TRUE(ledframe::glowLevel(128) > ledframe::glowLevel(200));
  TEST_ASSERT_EQUAL_UINT8(ledframe::glowLevel(255), ledframe::glowLevel(256));  // rounding
  TEST_ASSERT_TRUE(ledframe::glowLevel(255) <= 1);
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
  RUN_TEST(test_blur_spreads_light_to_neighbors);
  RUN_TEST(test_blur_with_smear_keeps_the_pixel);
  RUN_TEST(test_blur_stays_within_size);
  RUN_TEST(test_soft_dot_is_full_at_the_point);
  RUN_TEST(test_soft_dot_between_pixels_lights_both);
  RUN_TEST(test_soft_dot_lightens_and_stays_within_the_canvas);
  RUN_TEST(test_glow_level_falls_off_to_zero);
  return UNITY_END();
}

#include <unity.h>

#include <cstdio>
#include <vector>

#include "render/Layout.h"

using ledframe::Layout;
using ledframe::LayoutConfig;
using ledframe::Origin;
using ledframe::Rotation;

namespace {

using Grid = std::vector<std::vector<int>>;  // grid[row][column]

const Origin kOrigins[] = {Origin::kTopLeft, Origin::kTopRight, Origin::kBottomLeft,
                           Origin::kBottomRight};
const Rotation kRotations[] = {Rotation::k0, Rotation::k90, Rotation::k180, Rotation::k270};

// Rotates a grid 90 degrees clockwise.
Grid rotateClockwise(const Grid& in) {
  const size_t rows = in.size();
  const size_t cols = in[0].size();
  Grid out(cols, std::vector<int>(rows));
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      out[c][rows - 1 - r] = in[r][c];
    }
  }
  return out;
}

// Reference model that does not reuse the Layout formulas. It places logical pixel
// ids on the panel by mirroring and rotating the image, then walks the strip from the
// origin corner and records the strip position of every logical pixel.
std::vector<int> referenceTable(const LayoutConfig& cfg) {
  Grid image(cfg.height, std::vector<int>(cfg.width));
  for (int y = 0; y < cfg.height; ++y) {
    for (int x = 0; x < cfg.width; ++x) {
      const int srcX = cfg.flipX ? cfg.width - 1 - x : x;
      const int srcY = cfg.flipY ? cfg.height - 1 - y : y;
      image[srcY][srcX] = y * cfg.width + x;
    }
  }
  for (int turns = static_cast<int>(cfg.rotation) / 90; turns > 0; --turns) {
    image = rotateClockwise(image);
  }

  const int rows = static_cast<int>(image.size());
  const int cols = static_cast<int>(image[0].size());
  const bool fromBottom = cfg.origin == Origin::kBottomLeft || cfg.origin == Origin::kBottomRight;
  const bool fromRight = cfg.origin == Origin::kTopRight || cfg.origin == Origin::kBottomRight;

  std::vector<int> table(cfg.width * cfg.height, -1);
  int strip = 0;
  bool leftToRight = !fromRight;
  for (int step = 0; step < rows; ++step) {
    const int row = fromBottom ? rows - 1 - step : step;
    for (int i = 0; i < cols; ++i) {
      const int col = leftToRight ? i : cols - 1 - i;
      table[image[row][col]] = strip++;
    }
    if (cfg.serpentine) {
      leftToRight = !leftToRight;
    }
  }
  return table;
}

void expectTable(const LayoutConfig& cfg, const std::vector<int>& expected, const char* label) {
  Layout layout;
  TEST_ASSERT_TRUE_MESSAGE(layout.configure(cfg), label);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected.size(), layout.size(), label);
  for (size_t i = 0; i < expected.size(); ++i) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s, logical index %u", label,
                  static_cast<unsigned>(i));
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], layout.map(static_cast<int>(i)), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], layout.table()[i], message);
  }
}

void checkAllCombinations(uint16_t width, uint16_t height, bool flipX, bool flipY) {
  for (int serpentine = 0; serpentine < 2; ++serpentine) {
    for (Origin origin : kOrigins) {
      for (Rotation rotation : kRotations) {
        LayoutConfig cfg;
        cfg.width = width;
        cfg.height = height;
        cfg.serpentine = serpentine != 0;
        cfg.origin = origin;
        cfg.rotation = rotation;
        cfg.flipX = flipX;
        cfg.flipY = flipY;
        char label[120];
        std::snprintf(label, sizeof(label), "%ux%u serpentine=%d origin=%d rotation=%d flip=%d%d",
                      width, height, serpentine, static_cast<int>(origin),
                      static_cast<int>(rotation), flipX, flipY);
        const std::vector<int> expected = referenceTable(cfg);

        // Every physical index is used exactly once.
        std::vector<int> seen(expected.size(), 0);
        for (int index : expected) {
          TEST_ASSERT_TRUE_MESSAGE(index >= 0 && index < static_cast<int>(expected.size()), label);
          ++seen[index];
        }
        for (int count : seen) {
          TEST_ASSERT_EQUAL_INT_MESSAGE(1, count, label);
        }
        expectTable(cfg, expected, label);
      }
    }
  }
}

LayoutConfig make(uint16_t w, uint16_t h, bool serpentine, Origin origin, Rotation rotation) {
  LayoutConfig cfg;
  cfg.width = w;
  cfg.height = h;
  cfg.serpentine = serpentine;
  cfg.origin = origin;
  cfg.rotation = rotation;
  return cfg;
}

}  // namespace

void setUp() {}
void tearDown() {}

// Hand-computed tables for the 3x2 image  A B C / D E F  (logical indices 0..5).
void test_hand_computed_3x2() {
  expectTable(make(3, 2, false, Origin::kTopLeft, Rotation::k0), {0, 1, 2, 3, 4, 5}, "plain");
  expectTable(make(3, 2, true, Origin::kTopLeft, Rotation::k0), {0, 1, 2, 5, 4, 3}, "serpentine");
  expectTable(make(3, 2, false, Origin::kTopRight, Rotation::k0), {2, 1, 0, 5, 4, 3}, "top right");
  expectTable(make(3, 2, true, Origin::kBottomLeft, Rotation::k0), {5, 4, 3, 0, 1, 2},
              "bottom left serpentine");
  expectTable(make(3, 2, false, Origin::kBottomRight, Rotation::k0), {5, 4, 3, 2, 1, 0},
              "bottom right");
  // Rotated 90 degrees clockwise the panel rows are  D A / E B / F C.
  expectTable(make(3, 2, false, Origin::kTopLeft, Rotation::k90), {1, 3, 5, 0, 2, 4}, "rot 90");
  expectTable(make(3, 2, true, Origin::kTopLeft, Rotation::k90), {1, 2, 5, 0, 3, 4},
              "rot 90 serpentine");
  // Rotated 180 degrees the panel rows are  F E D / C B A.
  expectTable(make(3, 2, false, Origin::kTopLeft, Rotation::k180), {5, 4, 3, 2, 1, 0}, "rot 180");
  // Rotated 270 degrees the panel rows are  C F / B E / A D.
  expectTable(make(3, 2, false, Origin::kTopLeft, Rotation::k270), {4, 2, 0, 5, 3, 1}, "rot 270");

  LayoutConfig flipX = make(3, 2, false, Origin::kTopLeft, Rotation::k0);
  flipX.flipX = true;
  expectTable(flipX, {2, 1, 0, 5, 4, 3}, "flip x");
  LayoutConfig flipY = make(3, 2, false, Origin::kTopLeft, Rotation::k0);
  flipY.flipY = true;
  expectTable(flipY, {3, 4, 5, 0, 1, 2}, "flip y");
}

void test_all_combinations_3x2() {
  checkAllCombinations(3, 2, false, false);
}
void test_all_combinations_4x4() {
  checkAllCombinations(4, 4, false, false);
}

void test_all_combinations_with_flips() {
  checkAllCombinations(3, 2, true, false);
  checkAllCombinations(3, 2, false, true);
  checkAllCombinations(4, 4, true, true);
}

void test_default_matches_v1() {
  Layout layout;
  TEST_ASSERT_EQUAL_UINT16(15, layout.width());
  TEST_ASSERT_EQUAL_UINT16(10, layout.height());
  TEST_ASSERT_EQUAL_UINT16(0, layout.map(0, 0));
  TEST_ASSERT_EQUAL_UINT16(16, layout.map(1, 1));
  TEST_ASSERT_EQUAL_UINT16(149, layout.map(14, 9));
}

void test_out_of_range_coordinates() {
  Layout layout;
  TEST_ASSERT_TRUE(layout.configure(make(3, 2, true, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(-1, 0));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(0, -1));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(3, 0));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(0, 2));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(-1));
  TEST_ASSERT_EQUAL_UINT16(Layout::kInvalidIndex, layout.map(6));
  TEST_ASSERT_EQUAL_UINT16(3, layout.map(2, 1));
  TEST_ASSERT_EQUAL_UINT16(5, layout.map(0, 1));
}

void test_size_limits() {
  Layout layout;
  TEST_ASSERT_FALSE(layout.configure(make(0, 10, false, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_FALSE(layout.configure(make(10, 0, false, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_TRUE(
      layout.configure(make(ledframe::kMaxLeds, 1, false, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_EQUAL_UINT16(ledframe::kMaxLeds - 1, layout.map(ledframe::kMaxLeds - 1, 0));
  TEST_ASSERT_FALSE(
      layout.configure(make(ledframe::kMaxLeds + 1, 1, false, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_FALSE(layout.configure(make(0xFFFF, 0xFFFF, false, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_EQUAL_UINT16(ledframe::kMaxLeds, layout.size());
}

void test_invalid_enums_keep_previous_config() {
  Layout layout;
  TEST_ASSERT_TRUE(layout.configure(make(3, 2, true, Origin::kTopLeft, Rotation::k0)));
  TEST_ASSERT_FALSE(
      layout.configure(make(4, 4, false, Origin::kTopLeft, static_cast<Rotation>(45))));
  TEST_ASSERT_FALSE(layout.configure(make(4, 4, false, static_cast<Origin>(7), Rotation::k0)));
  TEST_ASSERT_EQUAL_UINT16(3, layout.width());
  TEST_ASSERT_EQUAL_UINT16(2, layout.height());
  TEST_ASSERT_TRUE(layout.config().serpentine);
  TEST_ASSERT_EQUAL_UINT16(5, layout.map(0, 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hand_computed_3x2);
  RUN_TEST(test_all_combinations_3x2);
  RUN_TEST(test_all_combinations_4x4);
  RUN_TEST(test_all_combinations_with_flips);
  RUN_TEST(test_default_matches_v1);
  RUN_TEST(test_out_of_range_coordinates);
  RUN_TEST(test_size_limits);
  RUN_TEST(test_invalid_enums_keep_previous_config);
  return UNITY_END();
}

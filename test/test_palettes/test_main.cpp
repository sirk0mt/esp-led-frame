#include <unity.h>

#include <cstring>

// Palettes.h brings crgb.h, which colorpalettes.h needs first.
#include "render/Gamma.h"
#include "render/Palettes.h"

#include <colorpalettes.h>

using ledframe::Palettes;

namespace {

Palettes palettes;

uint32_t packed(const CRGB& color) {
  return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

void expectColor(const CRGB& expected, const CRGB& actual) {
  TEST_ASSERT_EQUAL_HEX32(packed(expected), packed(actual));
}

// Palette blending may differ from the exact entry by a few steps per channel.
void expectNear(const CRGB& expected, const CRGB& actual) {
  TEST_ASSERT_UINT8_WITHIN(3, expected.r, actual.r);
  TEST_ASSERT_UINT8_WITHIN(3, expected.g, actual.g);
  TEST_ASSERT_UINT8_WITHIN(3, expected.b, actual.b);
}

void expectPalette(const CRGBPalette16& expected, const CRGBPalette16& actual) {
  for (int i = 0; i < 16; ++i) {
    expectColor(expected.entries[i], actual.entries[i]);
  }
}

}  // namespace

void setUp() {
  palettes.setUserColors(CRGB::Red, CRGB::Green, CRGB::Blue);
}
void tearDown() {}

void test_palette_count() {
  TEST_ASSERT_EQUAL_UINT8(8, Palettes::count());
  TEST_ASSERT_EQUAL_UINT8(Palettes::kCount, Palettes::count());
}

void test_ids_and_names_are_unique() {
  for (int i = 0; i < Palettes::kCount; ++i) {
    TEST_ASSERT_NOT_NULL(Palettes::id(i));
    TEST_ASSERT_NOT_NULL(Palettes::name(i));
    TEST_ASSERT_EQUAL_INT(i, Palettes::indexOf(Palettes::id(i)));
    for (int j = 0; j < i; ++j) {
      TEST_ASSERT_TRUE(std::strcmp(Palettes::id(i), Palettes::id(j)) != 0);
    }
  }
  TEST_ASSERT_EQUAL_STRING("rainbow", Palettes::id(0));
  TEST_ASSERT_EQUAL_STRING("custom", Palettes::id(Palettes::kUserIndex));
  TEST_ASSERT_EQUAL_STRING("Heat", Palettes::name(Palettes::indexOf("heat")));
  TEST_ASSERT_EQUAL_INT(-1, Palettes::indexOf("missing"));
  TEST_ASSERT_EQUAL_INT(-1, Palettes::indexOf(nullptr));
}

void test_valid_index_returns_builtin_palette() {
  expectPalette(CRGBPalette16(RainbowColors_p), palettes.get(Palettes::indexOf("rainbow")));
  expectPalette(CRGBPalette16(PartyColors_p), palettes.get(Palettes::indexOf("party")));
  expectPalette(CRGBPalette16(OceanColors_p), palettes.get(Palettes::indexOf("ocean")));
  expectPalette(CRGBPalette16(ForestColors_p), palettes.get(Palettes::indexOf("forest")));
  expectPalette(CRGBPalette16(LavaColors_p), palettes.get(Palettes::indexOf("lava")));
  expectPalette(CRGBPalette16(HeatColors_p), palettes.get(Palettes::indexOf("heat")));
  expectPalette(CRGBPalette16(CloudColors_p), palettes.get(Palettes::indexOf("cloud")));
}

void test_invalid_index_falls_back_to_default() {
  const int32_t invalid[] = {-1, 8, 100, INT32_MIN, INT32_MAX};
  for (int32_t index : invalid) {
    TEST_ASSERT_EQUAL_UINT8(Palettes::kDefaultIndex, Palettes::clampIndex(index));
    TEST_ASSERT_EQUAL_PTR(&palettes.get(Palettes::kDefaultIndex), &palettes.get(index));
    TEST_ASSERT_EQUAL_STRING("rainbow", Palettes::id(index));
  }
  TEST_ASSERT_EQUAL_UINT8(5, Palettes::clampIndex(5));
  TEST_ASSERT_EQUAL_UINT8(7, Palettes::clampIndex(7));
}

void test_user_palette_is_gradient_of_three_colors() {
  palettes.setUserColors(CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255));
  const CRGBPalette16& user = palettes.get(Palettes::kUserIndex);
  expectColor(CRGB(255, 0, 0), user.entries[0]);
  expectColor(CRGB(0, 0, 255), user.entries[15]);
  // The middle of the gradient is mostly green.
  TEST_ASSERT_TRUE(user.entries[8].g > user.entries[8].r);
  TEST_ASSERT_TRUE(user.entries[8].g > user.entries[8].b);
  expectColor(CRGB(0, 255, 0), palettes.userColor(1));
  expectColor(CRGB::Black, palettes.userColor(3));
}

void test_set_user_colors_changes_only_user_palette() {
  palettes.setUserColors(CRGB::White, CRGB::White, CRGB::White);
  for (int i = 0; i < 16; ++i) {
    expectColor(CRGB::White, palettes.get(Palettes::kUserIndex).entries[i]);
  }
  expectPalette(CRGBPalette16(RainbowColors_p), palettes.get(0));
}

void test_samples_span_whole_palette() {
  CRGB samples[16];
  TEST_ASSERT_EQUAL_UINT8(16, palettes.sample(Palettes::kUserIndex, samples, 16));
  expectColor(CRGB(255, 0, 0), samples[0]);
  // The last sample ends at the third color and does not wrap back to red.
  expectNear(CRGB(0, 0, 255), samples[15]);
  TEST_ASSERT_UINT8_WITHIN(3, 0, samples[15].r);

  const CRGBPalette16 heat(HeatColors_p);
  TEST_ASSERT_EQUAL_UINT8(4, palettes.sample(Palettes::indexOf("heat"), samples, 4));
  expectColor(heat.entries[0], samples[0]);
  expectNear(heat.entries[15], samples[3]);
}

void test_sample_edge_cases() {
  CRGB samples[2] = {CRGB::White, CRGB::White};
  TEST_ASSERT_EQUAL_UINT8(0, palettes.sample(0, samples, 0));
  TEST_ASSERT_EQUAL_UINT8(0, palettes.sample(0, nullptr, 4));
  expectColor(CRGB::White, samples[0]);
  TEST_ASSERT_EQUAL_UINT8(1, palettes.sample(0, samples, 1));
  expectColor(CRGBPalette16(RainbowColors_p).entries[0], samples[0]);
  // An invalid index samples the default palette.
  CRGB fallback[1];
  TEST_ASSERT_EQUAL_UINT8(1, palettes.sample(-5, fallback, 1));
  expectColor(samples[0], fallback[0]);
}

// --- Gamma correction of user colors --------------------------------------------------

// The ends stay exact and the curve never falls. It cannot rise at every step: the
// darkest 15 values all map to 0.
void test_gamma_table() {
  TEST_ASSERT_EQUAL_UINT8(0, ledframe::gammaCorrect(static_cast<uint8_t>(0)));
  TEST_ASSERT_EQUAL_UINT8(255, ledframe::gammaCorrect(static_cast<uint8_t>(255)));
  TEST_ASSERT_EQUAL_UINT8(56, ledframe::gammaCorrect(static_cast<uint8_t>(128)));  // #808080
  TEST_ASSERT_EQUAL_UINT8(1, ledframe::gammaCorrect(static_cast<uint8_t>(15)));
  for (int value = 1; value < 256; ++value) {
    TEST_ASSERT_TRUE(ledframe::gammaCorrect(static_cast<uint8_t>(value)) >=
                     ledframe::gammaCorrect(static_cast<uint8_t>(value - 1)));
    TEST_ASSERT_TRUE(ledframe::gammaCorrect(static_cast<uint8_t>(value)) <= value);
  }
  // The colors of the hardware check: pink and orange.
  expectColor(CRGB(255, 137, 154), ledframe::gammaCorrect(CRGB(255, 192, 203)));
  expectColor(CRGB(255, 98, 0), ledframe::gammaCorrect(CRGB(255, 165, 0)));
}

// With gamma on, only the user palette comes corrected for rendering. The gradient
// runs between the chosen colors, then each entry is corrected. The samples for the
// UI keep the chosen colors.
void test_gamma_corrects_only_the_user_palette() {
  Palettes local;
  local.setUserColors(CRGB(255, 0, 0), CRGB(128, 128, 128), CRGB(0, 0, 255));
  const CRGBPalette16 raw = local.get(Palettes::kUserIndex);
  expectColor(CRGB(136, 119, 119), raw.entries[7]);  // between red and gray
  expectColor(CRGB(119, 119, 136), raw.entries[8]);  // between gray and blue

  local.setGamma(true);
  const CRGBPalette16& corrected = local.get(Palettes::kUserIndex);
  for (uint8_t i = 0; i < 16; ++i) {
    expectColor(ledframe::gammaCorrect(raw.entries[i]), corrected.entries[i]);
  }
  expectColor(CRGB(64, 48, 48), corrected.entries[7]);
  expectColor(CRGB(48, 48, 64), corrected.entries[8]);
  expectColor(CRGB(255, 0, 0), corrected.entries[0]);  // full channels stay
  expectColor(CRGBPalette16(LavaColors_p).entries[5], local.get(4).entries[5]);

  CRGB corrected3[3];
  TEST_ASSERT_EQUAL_UINT8(3, local.sample(Palettes::kUserIndex, corrected3, 3));

  local.setGamma(false);
  expectColor(raw.entries[7], local.get(Palettes::kUserIndex).entries[7]);
  CRGB plain3[3];
  TEST_ASSERT_EQUAL_UINT8(3, local.sample(Palettes::kUserIndex, plain3, 3));
  for (uint8_t i = 0; i < 3; ++i) {
    expectColor(plain3[i], corrected3[i]);  // The samples never change with gamma.
  }
  expectColor(CRGB(255, 0, 0), plain3[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_palette_count);
  RUN_TEST(test_ids_and_names_are_unique);
  RUN_TEST(test_valid_index_returns_builtin_palette);
  RUN_TEST(test_invalid_index_falls_back_to_default);
  RUN_TEST(test_user_palette_is_gradient_of_three_colors);
  RUN_TEST(test_set_user_colors_changes_only_user_palette);
  RUN_TEST(test_samples_span_whole_palette);
  RUN_TEST(test_sample_edge_cases);
  RUN_TEST(test_gamma_table);
  RUN_TEST(test_gamma_corrects_only_the_user_palette);
  return UNITY_END();
}

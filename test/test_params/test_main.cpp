#include <unity.h>

#include "effects/Effect.h"

using ledframe::clampParam;
using ledframe::defaultParam;
using ledframe::EffectDescriptor;
using ledframe::ParamKind;
using ledframe::ParamSpec;

namespace {

constexpr ParamSpec kSlider = {"speed", "Speed", ParamKind::Slider, -10, 100, 40};
constexpr ParamSpec kToggle = {"mirror", "Mirror", ParamKind::Toggle, 0, 1, 1};
constexpr ParamSpec kColor = {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0x123456};
constexpr ParamSpec kSelect = {"mode", "Mode", ParamKind::Select, 0, 0, 1, "random|palette|solid"};
constexpr ParamSpec kSelectOne = {"one", "One", ParamKind::Select, 0, 0, 0, "only"};
constexpr ParamSpec kSelectNone = {"none", "None", ParamKind::Select, 0, 0, 0, nullptr};
constexpr ParamSpec kPalette = {"palette", "Palette", ParamKind::Palette, 0, 0, 2};

constexpr ParamSpec kAll[] = {kSlider, kToggle, kColor, kSelect, kPalette};

ledframe::Effect* noFactory(void* /*memory*/) {
  return nullptr;
}
const EffectDescriptor kDescriptor = {"all", "All", 1, 0, kAll, 5, nullptr, &noFactory};

}  // namespace

void setUp() {}
void tearDown() {}

void test_slider_clamps_to_range() {
  TEST_ASSERT_EQUAL_INT32(-10, clampParam(kSlider, -11));
  TEST_ASSERT_EQUAL_INT32(-10, clampParam(kSlider, INT32_MIN));
  TEST_ASSERT_EQUAL_INT32(-10, clampParam(kSlider, -10));
  TEST_ASSERT_EQUAL_INT32(55, clampParam(kSlider, 55));
  TEST_ASSERT_EQUAL_INT32(100, clampParam(kSlider, 100));
  TEST_ASSERT_EQUAL_INT32(100, clampParam(kSlider, 101));
  TEST_ASSERT_EQUAL_INT32(100, clampParam(kSlider, INT32_MAX));
}

void test_toggle_is_boolean() {
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kToggle, 0));
  TEST_ASSERT_EQUAL_INT32(1, clampParam(kToggle, 1));
  TEST_ASSERT_EQUAL_INT32(1, clampParam(kToggle, 7));
  TEST_ASSERT_EQUAL_INT32(1, clampParam(kToggle, -1));
}

void test_color_is_masked_to_24_bits() {
  TEST_ASSERT_EQUAL_INT32(0xABCDEF, clampParam(kColor, 0xABCDEF));
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, clampParam(kColor, 0x7FFFFFFF));
  TEST_ASSERT_EQUAL_INT32(0x345678, clampParam(kColor, 0x12345678));
  TEST_ASSERT_EQUAL_INT32(0xFFFFFF, clampParam(kColor, -1));
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kColor, 0));
}

void test_select_clamps_to_option_count() {
  TEST_ASSERT_EQUAL_UINT8(3, ledframe::selectOptionCount(kSelect));
  TEST_ASSERT_EQUAL_UINT8(1, ledframe::selectOptionCount(kSelectOne));
  TEST_ASSERT_EQUAL_UINT8(0, ledframe::selectOptionCount(kSelectNone));
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kSelect, -1));
  TEST_ASSERT_EQUAL_INT32(2, clampParam(kSelect, 2));
  TEST_ASSERT_EQUAL_INT32(2, clampParam(kSelect, 3));
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kSelectOne, 5));
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kSelectNone, 5));
}

void test_palette_is_not_negative() {
  TEST_ASSERT_EQUAL_INT32(0, clampParam(kPalette, -3));
  TEST_ASSERT_EQUAL_INT32(4, clampParam(kPalette, 4));
}

void test_defaults() {
  TEST_ASSERT_EQUAL_INT32(40, defaultParam(kSlider));
  TEST_ASSERT_EQUAL_INT32(1, defaultParam(kToggle));
  TEST_ASSERT_EQUAL_INT32(0x123456, defaultParam(kColor));
  TEST_ASSERT_EQUAL_INT32(1, defaultParam(kSelect));
  TEST_ASSERT_EQUAL_INT32(2, defaultParam(kPalette));

  int32_t values[ledframe::kMaxParams] = {};
  ledframe::loadDefaults(kDescriptor, values);
  const int32_t expected[] = {40, 1, 0x123456, 1, 2};
  TEST_ASSERT_EQUAL_INT32_ARRAY(expected, values, 5);
  TEST_ASSERT_EQUAL_INT32(0, values[5]);  // Entries past paramCount are untouched.
}

void test_clamp_params_in_place() {
  int32_t values[] = {500, 9, 0x1FFFFFF, 99, -1};
  ledframe::clampParams(kDescriptor, values);
  const int32_t expected[] = {100, 1, 0xFFFFFF, 2, 0};
  TEST_ASSERT_EQUAL_INT32_ARRAY(expected, values, 5);
}

void test_find_param_by_id() {
  TEST_ASSERT_EQUAL_INT(0, ledframe::findParam(kDescriptor, "speed"));
  TEST_ASSERT_EQUAL_INT(4, ledframe::findParam(kDescriptor, "palette"));
  TEST_ASSERT_EQUAL_INT(-1, ledframe::findParam(kDescriptor, "missing"));
  TEST_ASSERT_EQUAL_INT(-1, ledframe::findParam(kDescriptor, nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slider_clamps_to_range);
  RUN_TEST(test_toggle_is_boolean);
  RUN_TEST(test_color_is_masked_to_24_bits);
  RUN_TEST(test_select_clamps_to_option_count);
  RUN_TEST(test_palette_is_not_negative);
  RUN_TEST(test_defaults);
  RUN_TEST(test_clamp_params_in_place);
  RUN_TEST(test_find_param_by_id);
  return UNITY_END();
}

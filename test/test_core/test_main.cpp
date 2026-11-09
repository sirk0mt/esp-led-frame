#include <FastLED.h>
#include <unity.h>

#include <cstring>

#include "core/Log.h"
#include "core/Version.h"

void setUp() {}
void tearDown() {}

void test_version_is_v2_dev() {
  TEST_ASSERT_EQUAL_STRING("2.3.4", ledframe::kVersion);
  TEST_ASSERT_TRUE(std::strlen(ledframe::kBuildDate) > 0);
}

void test_log_macros_compile_and_run() {
  LOG_E("test", "error %d", 1);
  LOG_W("test", "warn %s", "two");
  LOG_I("test", "info");
  LOG_D("test", "debug %u", 4u);
  TEST_PASS();
}

void test_fastled_types_available() {
  CRGB color = CRGB::Red;
  TEST_ASSERT_EQUAL_UINT8(255, color.r);
  TEST_ASSERT_EQUAL_UINT8(0, color.g);
  TEST_ASSERT_EQUAL_UINT8(0, color.b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_version_is_v2_dev);
  RUN_TEST(test_log_macros_compile_and_run);
  RUN_TEST(test_fastled_types_available);
  return UNITY_END();
}

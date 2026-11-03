#include <unity.h>

#include <cstdio>
#include <cstring>

#include "app/ConsoleParser.h"

using ledframe::Command;
using ledframe::CommandType;
using ledframe::ConsoleAction;
using ledframe::ConsoleResult;
using ledframe::EffectRegistry;
using ledframe::parseConsoleLine;

// Unity compares integers; enum class values need an explicit cast.
#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

namespace {

const EffectRegistry& registry() {
  return EffectRegistry::global();
}

uint8_t solid() {
  return static_cast<uint8_t>(registry().indexOf("solid"));
}

ConsoleResult parse(const char* line) {
  return parseConsoleLine(line, registry(), solid());
}

void expectError(const char* line) {
  const ConsoleResult result = parse(line);
  ASSERT_ENUM(ConsoleAction::kError, result.action);
  TEST_ASSERT_NOT_NULL(result.error);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_empty_lines() {
  ASSERT_ENUM(ConsoleAction::kNone, parse("").action);
  ASSERT_ENUM(ConsoleAction::kNone, parse("   \t ").action);
  ASSERT_ENUM(ConsoleAction::kNone, parse("\r\n").action);
}

void test_effect() {
  const ConsoleResult result = parse("effect solid");
  ASSERT_ENUM(ConsoleAction::kEngine, result.action);
  ASSERT_ENUM(CommandType::SetEffect, result.command.type);
  TEST_ASSERT_EQUAL_UINT8(solid(), result.command.effect);
  TEST_ASSERT_EQUAL_STRING("unknown effect", parse("effect missing").error);
  expectError("effect");
  expectError("effect solid extra");
}

void test_brightness() {
  ConsoleResult result = parse("  bri   50  ");
  ASSERT_ENUM(ConsoleAction::kEngine, result.action);
  ASSERT_ENUM(CommandType::SetBrightness, result.command.type);
  TEST_ASSERT_EQUAL_UINT8(50, result.command.brightness);
  TEST_ASSERT_EQUAL_UINT8(255, parse("bri 255").command.brightness);
  TEST_ASSERT_EQUAL_UINT8(0, parse("bri 0").command.brightness);
  TEST_ASSERT_EQUAL_UINT8(16, parse("bri 0x10").command.brightness);
  expectError("bri 256");
  expectError("bri -1");
  expectError("bri 12x");
  expectError("bri");
  expectError("bri 1 2");
}

void test_param_of_active_effect() {
  const ConsoleResult result = parse("param color 0xFF0000");
  ASSERT_ENUM(ConsoleAction::kEngine, result.action);
  ASSERT_ENUM(CommandType::SetParams, result.command.type);
  TEST_ASSERT_EQUAL_UINT8(solid(), result.command.effect);  // Explicit target.
  TEST_ASSERT_EQUAL_UINT8(1, result.command.paramCount);
  TEST_ASSERT_EQUAL_UINT8(0, result.command.params[0].index);
  TEST_ASSERT_EQUAL_INT32(0xFF0000, result.command.params[0].value);

  // Out-of-range values are accepted here; the engine clamps them.
  TEST_ASSERT_EQUAL_INT32(-5, parse("param color -5").command.params[0].value);

  TEST_ASSERT_EQUAL_STRING("unknown parameter", parse("param speed 1").error);
  expectError("param color");
  expectError("param color red");
  expectError("param color 0x");
  expectError("param color 99999999999");
}

void test_param_without_active_effect() {
  const ConsoleResult result = parseConsoleLine("param color 1", registry(), 0xFF);
  ASSERT_ENUM(ConsoleAction::kError, result.action);
  TEST_ASSERT_EQUAL_STRING("no active effect", result.error);
}

void test_color_order() {
  for (uint8_t i = 0; i <= 5; ++i) {
    char line[16];
    std::snprintf(line, sizeof(line), "order %u", static_cast<unsigned>(i));
    const ConsoleResult result = parse(line);
    ASSERT_ENUM(ConsoleAction::kColorOrder, result.action);
    TEST_ASSERT_EQUAL_UINT8(i, result.colorOrder);
  }
  expectError("order 6");
  expectError("order");
  expectError("order grb");
}

void test_test_and_info() {
  ASSERT_ENUM(ConsoleAction::kTestPattern, parse("test").action);
  ASSERT_ENUM(ConsoleAction::kInfo, parse("info").action);
  expectError("test 1");
  expectError("info now");
}

void test_wifi() {
  ConsoleResult result = parse("wifi HomeNet secret123");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("HomeNet", result.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("secret123", result.wifiPassword);

  result = parse("wifi OpenCafe");  // Open network.
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("", result.wifiPassword);

  result = parse("wifi \"  \"");  // Spaces only: no network, access point only.
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("", result.wifiSsid);
  result = parse("wifi \" Home \" secret123");  // Spaces around a name stay.
  TEST_ASSERT_EQUAL_STRING(" Home ", result.wifiSsid);

  // Longest accepted line: 32-byte ssid and 64-byte password.
  char line[ledframe::kConsoleLineMax + 1];
  std::snprintf(line, sizeof(line), "wifi %s %s", "ssid5678901234567890123456789012",
                "pass567890123456789012345678901234567890123456789012345678901234");
  result = parse(line);
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_UINT32(32, std::strlen(result.wifiSsid));
  TEST_ASSERT_EQUAL_UINT32(64, std::strlen(result.wifiPassword));

  TEST_ASSERT_EQUAL_STRING("password must have 8 to 64 characters",
                           parse("wifi HomeNet short").error);
  TEST_ASSERT_EQUAL_STRING("ssid longer than 32 characters",
                           parse("wifi ssid567890123456789012345678901234 secret123").error);
  expectError("wifi");
  expectError("wifi a b c");
}

void test_quoted_words() {
  // The author's network: spaces and an apostrophe.
  ConsoleResult result = parse("wifi \"There's no place like 127.0.0.1\" secret123");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("There's no place like 127.0.0.1", result.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("secret123", result.wifiPassword);

  result = parse("  wifi   \"My Net\"   \"pass word 1\"  ");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("My Net", result.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("pass word 1", result.wifiPassword);

  // Escapes inside quotes.
  result = parse("wifi \"Say \\\"hi\\\" \\\\o/\" secret123");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("Say \"hi\" \\o/", result.wifiSsid);

  // Empty quotes: an empty token (open network, or "" ssid for AP only).
  result = parse("wifi OpenCafe \"\"");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("", result.wifiPassword);
  result = parse("wifi \"\"");
  ASSERT_ENUM(ConsoleAction::kWifi, result.action);
  TEST_ASSERT_EQUAL_STRING("", result.wifiSsid);

  // Quotes work for every command.
  result = parse("preset save \"Wieczór ciepły\"");
  ASSERT_ENUM(ConsoleAction::kPresetSave, result.action);
  TEST_ASSERT_EQUAL_STRING("Wieczór ciepły", result.presetName);
  TEST_ASSERT_EQUAL_UINT8(77, parse("bri \"77\"").command.brightness);

  // A quote inside a plain word is just a character.
  result = parse("wifi it's secret123");
  TEST_ASSERT_EQUAL_STRING("it's", result.wifiSsid);

  TEST_ASSERT_EQUAL_STRING("missing closing quote", parse("wifi \"Open end secret123").error);
  TEST_ASSERT_EQUAL_STRING("space expected after closing quote",
                           parse("wifi \"My\"Net secret123").error);
  expectError("wifi \"a\" \"b\" \"c\"");  // Too many arguments.
}

void test_presets() {
  ConsoleResult result = parse("preset save Evening");
  ASSERT_ENUM(ConsoleAction::kPresetSave, result.action);
  TEST_ASSERT_EQUAL_STRING("Evening", result.presetName);

  result = parse("preset apply 12");
  ASSERT_ENUM(ConsoleAction::kPresetApply, result.action);
  TEST_ASSERT_EQUAL_UINT8(12, result.presetId);
  TEST_ASSERT_EQUAL_UINT8(255, parse("preset apply 0xFF").presetId);

  TEST_ASSERT_EQUAL_STRING("invalid preset name",
                           parse("preset save 012345678901234567890123456789012").error);
  expectError("preset");
  expectError("preset save");
  expectError("preset save two words");
  expectError("preset apply");
  expectError("preset apply 0");
  expectError("preset apply 256");
  expectError("preset apply first");
  expectError("preset delete 1");
  TEST_ASSERT_NOT_NULL(std::strstr(ledframe::kConsoleHelp, "preset apply <id>"));
}

void test_unknown_and_long_lines() {
  TEST_ASSERT_EQUAL_STRING("unknown command", parse("reboot").error);
  expectError("EFFECT solid");  // Commands are lowercase.
  expectError("bri 1 2 3 4 5");

  char longLine[ledframe::kConsoleLineMax + 2];
  std::memset(longLine, 'a', sizeof(longLine) - 1);
  longLine[sizeof(longLine) - 1] = '\0';
  TEST_ASSERT_EQUAL_STRING("line too long", parse(longLine).error);
  TEST_ASSERT_NOT_NULL(std::strstr(ledframe::kConsoleHelp, "order <0-5>"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_lines);
  RUN_TEST(test_effect);
  RUN_TEST(test_brightness);
  RUN_TEST(test_param_of_active_effect);
  RUN_TEST(test_param_without_active_effect);
  RUN_TEST(test_color_order);
  RUN_TEST(test_test_and_info);
  RUN_TEST(test_wifi);
  RUN_TEST(test_quoted_words);
  RUN_TEST(test_presets);
  RUN_TEST(test_unknown_and_long_lines);
  return UNITY_END();
}

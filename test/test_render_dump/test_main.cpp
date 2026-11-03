// Effect frames for viewing without hardware.
//
// Not a test of behavior: with LEDFRAME_DUMP set to a file path it renders effects
// through EffectEngine with a fake clock and writes every frame to that file as text,
// for scripts/show_effect.py. Without LEDFRAME_DUMP it does nothing and passes, so
// `pio test -e native` and CI are unchanged.
//
//   LEDFRAME_DUMP          output file (required to render anything)
//   LEDFRAME_DUMP_EFFECTS  "fire:speed=100,cooling=80;radial-rainbow"; default: every
//                          effect that the UI shows, with default parameters
//   LEDFRAME_DUMP_MS       time to render, default 3000
//   LEDFRAME_DUMP_FPS      frames per second, default 25
//   LEDFRAME_DUMP_SIZE     matrix size, default "15x10"
//   LEDFRAME_DUMP_GAMMA    "1": user colors with gamma correction
//
// File format (version 1), one block per effect:
//   effect <id> <width> <height> <fps>
//   params <id>=<value> ...
//   frame <index> <nowMs>
//   <height lines of width "RRGGBB" words, row by row from the top>
//   end
#include <lib8tion.h>
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "effects/EffectEngine.h"

using ledframe::Command;
using ledframe::EffectDescriptor;
using ledframe::EffectEngine;
using ledframe::EffectRegistry;
using ledframe::Frame;
using ledframe::IFrameSink;
using ledframe::LayoutConfig;
using ledframe::Palettes;

namespace {

class NullSink : public IFrameSink {
 public:
  void show(const Frame& /*frame*/) override {}
};

uint32_t envNumber(const char* name, uint32_t fallback) {
  const char* text = std::getenv(name);
  return text != nullptr && *text != '\0' ? static_cast<uint32_t>(std::strtoul(text, nullptr, 10))
                                          : fallback;
}

// "123", "0x7f", "#FF8000" or a Select option name ("inward").
bool parseValue(const ledframe::ParamSpec& spec, const std::string& text, int32_t& out) {
  if (text.empty()) {
    return false;
  }
  if (text[0] == '#') {
    out = static_cast<int32_t>(std::strtol(text.c_str() + 1, nullptr, 16));
    return true;
  }
  char* end = nullptr;
  const long number = std::strtol(text.c_str(), &end, 0);
  if (end != nullptr && *end == '\0') {
    out = static_cast<int32_t>(number);
    return true;
  }
  if (spec.options == nullptr) {
    return false;
  }
  int32_t index = 0;
  const char* option = spec.options;
  while (*option != '\0') {
    const char* bar = std::strchr(option, '|');
    const size_t length = bar != nullptr ? static_cast<size_t>(bar - option) : std::strlen(option);
    if (text.size() == length && std::strncmp(option, text.c_str(), length) == 0) {
      out = index;
      return true;
    }
    if (bar == nullptr) {
      break;
    }
    option = bar + 1;
    ++index;
  }
  return false;
}

// Renders one "id:name=value,..." spec and appends it to file.
void dumpEffect(const std::string& spec, std::FILE* file) {
  const size_t colon = spec.find(':');
  const std::string id = spec.substr(0, colon);
  const EffectRegistry& registry = EffectRegistry::global();
  const int index = registry.indexOf(id.c_str());
  if (index < 0) {
    std::printf("render_dump: unknown effect '%s'\n", id.c_str());
    TEST_FAIL_MESSAGE("unknown effect in LEDFRAME_DUMP_EFFECTS");
  }
  const EffectDescriptor& descriptor = *registry.at(static_cast<uint8_t>(index));

  static NullSink sink;
  static Palettes palettes;
  static EffectEngine engine(registry, palettes, sink);  // static: about 17 KB
  palettes.setUserColors(CRGB::Red, CRGB::Green, CRGB::Blue);

  LayoutConfig layout;
  const char* size = std::getenv("LEDFRAME_DUMP_SIZE");
  unsigned width = 15;
  unsigned height = 10;
  if (size != nullptr && std::sscanf(size, "%ux%u", &width, &height) != 2) {
    TEST_FAIL_MESSAGE("LEDFRAME_DUMP_SIZE must look like 15x10");
  }
  layout.width = static_cast<uint16_t>(width);
  layout.height = static_cast<uint16_t>(height);
  const uint32_t fps = envNumber("LEDFRAME_DUMP_FPS", 25);
  const uint32_t durationMs = envNumber("LEDFRAME_DUMP_MS", 3000);
  engine.setFps(static_cast<uint8_t>(fps));
  engine.setGamma(envNumber("LEDFRAME_DUMP_GAMMA", 0) != 0);
  engine.apply(Command::setTransition(0));
  // Before the layout: a new layout creates the active effect again, and effects seed
  // their randomness when they are created.
  random16_set_seed(1234);
  engine.apply(Command::setLayout(layout));

  // Defaults first, then the given values. The engine clamps them like API values.
  Command defaults = Command::setParams(static_cast<uint8_t>(index));
  int32_t values[ledframe::kMaxParams];
  ledframe::loadDefaults(descriptor, values);
  for (uint8_t p = 0; p < descriptor.paramCount; ++p) {
    defaults.addParam(p, values[p]);
  }
  engine.apply(defaults);
  if (colon != std::string::npos) {
    Command given = Command::setParams(static_cast<uint8_t>(index));
    size_t start = colon + 1;
    while (start < spec.size()) {
      size_t comma = spec.find(',', start);
      if (comma == std::string::npos) {
        comma = spec.size();
      }
      const std::string pair = spec.substr(start, comma - start);
      const size_t equals = pair.find('=');
      const int param = equals != std::string::npos
                            ? ledframe::findParam(descriptor, pair.substr(0, equals).c_str())
                            : -1;
      int32_t value = 0;
      if (param < 0 || !parseValue(descriptor.params[param], pair.substr(equals + 1), value)) {
        std::printf("render_dump: bad parameter '%s' for '%s'\n", pair.c_str(), id.c_str());
        TEST_FAIL_MESSAGE("bad parameter in LEDFRAME_DUMP_EFFECTS");
      }
      given.addParam(static_cast<uint8_t>(param), value);
      start = comma + 1;
    }
    engine.apply(given);
  }

  random16_set_seed(1234);  // again: recreating the previous effect drew numbers
  engine.apply(Command::setEffect(static_cast<uint8_t>(index)));

  std::fprintf(file, "effect %s %u %u %u\nparams", descriptor.id, width, height,
               static_cast<unsigned>(fps));
  const int32_t* stored = engine.params(static_cast<uint8_t>(index));
  for (uint8_t p = 0; p < descriptor.paramCount; ++p) {
    std::fprintf(file, " %s=%ld", descriptor.params[p].id, static_cast<long>(stored[p]));
  }
  std::fprintf(file, "\n");

  // The fake clock starts late, so the engine's schedule does not depend on earlier
  // effects; frame k is drawn at start + k * 1000 / fps.
  static uint32_t clockMs = 100000;
  clockMs += 100000;
  const uint32_t frames = durationMs * fps / 1000;
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const uint32_t nowMs = clockMs + frame * 1000 / fps;
    engine.tick(nowMs);
    const ledframe::Canvas& canvas = engine.output();
    std::fprintf(file, "frame %u %u\n", static_cast<unsigned>(frame),
                 static_cast<unsigned>(nowMs - clockMs));
    for (uint16_t y = 0; y < canvas.height(); ++y) {
      for (uint16_t x = 0; x < canvas.width(); ++x) {
        const CRGB& pixel = canvas.at(x, y);
        std::fprintf(file, x == 0 ? "%02X%02X%02X" : " %02X%02X%02X", pixel.r, pixel.g, pixel.b);
      }
      std::fprintf(file, "\n");
    }
  }
  std::fprintf(file, "end\n");
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_render_dump() {
  const char* path = std::getenv("LEDFRAME_DUMP");
  if (path == nullptr || *path == '\0') {
    TEST_PASS_MESSAGE("LEDFRAME_DUMP not set: nothing rendered");
  }
  std::FILE* file = std::fopen(path, "w");
  TEST_ASSERT_NOT_NULL_MESSAGE(file, "cannot write LEDFRAME_DUMP");
  std::fprintf(file, "ledframe-dump 1\n");

  std::string specs;
  const char* chosen = std::getenv("LEDFRAME_DUMP_EFFECTS");
  if (chosen != nullptr && *chosen != '\0') {
    specs = chosen;
  } else {
    for (const EffectDescriptor* descriptor : EffectRegistry::global()) {
      if ((descriptor->flags & ledframe::kFlagHidden) == 0) {
        specs += std::string(specs.empty() ? "" : ";") + descriptor->id;
      }
    }
  }
  size_t start = 0;
  while (start < specs.size()) {
    size_t end = specs.find(';', start);
    if (end == std::string::npos) {
      end = specs.size();
    }
    if (end > start) {
      dumpEffect(specs.substr(start, end - start), file);
    }
    start = end + 1;
  }
  std::fclose(file);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_render_dump);
  return UNITY_END();
}

// Compile-time limits shared by all modules.
#pragma once

#include <cstdint>

#ifndef MAX_LEDS
#define MAX_LEDS 1024
#endif

namespace ledframe {

// Maximum number of LEDs (width * height). Set with -DMAX_LEDS in platformio.ini.
inline constexpr uint16_t kMaxLeds = MAX_LEDS;

static_assert(MAX_LEDS > 0, "MAX_LEDS must be positive");
static_assert(MAX_LEDS < 0xFFFF, "MAX_LEDS must fit in uint16_t below the invalid index marker");

}  // namespace ledframe

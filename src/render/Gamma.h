// Gamma correction for colors chosen on a screen.
//
// A color picker gives sRGB values, which a screen shows with a gamma of about 2.2.
// LEDs are linear, so #808080 looks almost white on them and pastel colors look
// washed out. gammaCorrect() maps a channel to the LED value that looks like the
// color on a screen. Only user colors use it (Color parameters, the paint picture,
// the custom palette): the built-in palettes are already made for LEDs.
#pragma once

#include <crgb.h>

#include <cstdint>

namespace ledframe {

// round(255 * (value / 255) ^ 2.2): 0 -> 0, 128 -> 56, 255 -> 255, never falling.
uint8_t gammaCorrect(uint8_t value);
CRGB gammaCorrect(const CRGB& color);

}  // namespace ledframe

// Hardware-independent part of the LED output, tested natively.
//
// Effects draw logical RGB. Only this layer knows the physical pixel order (Layout)
// and the channel order of the strip, so color order is handled in exactly one place.
#pragma once

#include <crgb.h>

#include <cstdint>
#include <utility>

#include "render/IFrameSink.h"

#ifndef LED_PIN
#error "LED_PIN must be set in platformio.ini build_flags"
#endif

namespace ledframe {

// Order in which the strip reads the three color bytes. The numbers are stable
// (v1 used the same ones) and are stored in the configuration.
enum class ColorOrder : uint8_t { kRGB = 0, kRBG = 1, kGRB = 2, kGBR = 3, kBRG = 4, kBGR = 5 };

inline constexpr uint8_t kColorOrderCount = 6;
// RGB: the order that shows the test pattern correctly on the author's frame.
inline constexpr ColorOrder kDefaultColorOrder = ColorOrder::kRGB;

inline bool isValidColorOrder(uint8_t value) {
  return value < kColorOrderCount;
}

// GPIOs that can drive the strip data line, chosen in the configuration. FastLED
// takes the pin as a template argument, so LedOutput registers the strip through
// this list and a new pin applies after a restart. Left out on the ESP32: 0 and 12
// (boot strapping: a level shifter pulling them up stops the boot), 1 and 3 (serial
// console), 6-11 (flash) and 34-39 (input only).
using LedPins = std::integer_sequence<uint8_t, 2, 4, 5, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23,
                                      25, 26, 27, 32, 33>;

template <uint8_t... Pins>
constexpr bool isLedPinIn(uint8_t pin, std::integer_sequence<uint8_t, Pins...>) {
  return ((pin == Pins) || ...);
}

constexpr bool isValidLedPin(uint8_t pin) {
  return isLedPinIn(pin, LedPins{});
}

// The pin used when the configuration has none: LED_PIN from platformio.ini.
inline constexpr uint8_t kDefaultLedPin = LED_PIN;
static_assert(isValidLedPin(kDefaultLedPin), "LED_PIN must be one of the pins in LedPins");

// Name for logs and the UI ("RGB", "GRB", ...). An invalid value returns "?".
const char* colorOrderName(ColorOrder order);

// Reorders a logical color for a strip with the given channel order. The FastLED
// controller is registered as RGB, so byte 0 of the result is sent first.
CRGB applyColorOrder(const CRGB& color, ColorOrder order);

// Writes every logical pixel of the frame to its physical position, in strip
// channel order. `physical` must hold frame.canvas.size() pixels.
void mapFrame(const Frame& frame, ColorOrder order, CRGB* physical);

// Test pattern in physical strip order and logical colors: the first pixel (where
// the strip starts) is white, the rest is split into red, green and blue thirds.
void fillTestPattern(CRGB* physical, uint16_t count);

}  // namespace ledframe

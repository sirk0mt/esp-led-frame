// FastLED implementation of IFrameSink (ESP32 only, not compiled natively).
//
// The strip is a WS2812B on a pin from LedPins, chosen once in begin(). FastLED is
// registered with RGB order and the channel permutation is applied in software
// (OutputMapping), so the color order can change at run time without restarting
// the device.
#pragma once

#include <crgb.h>

#include <cstdint>

#include "core/Limits.h"
#include "render/IFrameSink.h"
#include "render/OutputMapping.h"

namespace fl {
class CLEDController;
}  // namespace fl

namespace ledframe {

class LedOutput : public IFrameSink {
 public:
  static constexpr uint8_t kDefaultVolts = 5;
  static constexpr uint32_t kDefaultMilliamps = 2000;

  // Registers the strip on GPIO `pin` with FastLED: color correction, dithering,
  // power limit. A pin outside LedPins falls back to kDefaultLedPin. FastLED cannot
  // move a strip to another pin, so begin() is called once.
  void begin(uint8_t pin);
  uint8_t pin() const { return pin_; }

  // Sends the frame: layout mapping, channel order, global brightness.
  void show(const Frame& frame) override;

  // Shows fillTestPattern() on `count` LEDs, bypassing the layout but using the
  // channel order: with the right order the strip shows red, green and blue thirds
  // and a white first pixel.
  void showTestPattern(uint16_t count, uint8_t brightness);

  // Returns false and keeps the current order when value is not 0..5. The new
  // order is used from the next show().
  bool setColorOrder(uint8_t value);
  ColorOrder colorOrder() const { return order_; }

  // Limits the power drawn by the strip. milliamps = 0 disables the limit.
  void setPowerLimit(uint8_t volts, uint32_t milliamps);

  // FastLED temporal dithering, from the next show(). It smooths dim colors over
  // frames, but a frame sent once keeps one dithering step.
  void setDither(bool on);

 private:
  // Changes the number of LEDs FastLED sends. When the count shrinks, the LEDs
  // that are no longer used are switched off first.
  void setActiveCount(uint16_t count);

  CRGB leds_[kMaxLeds];
  fl::CLEDController* controller_ = nullptr;
  uint16_t activeCount_ = 0;
  uint8_t pin_ = kDefaultLedPin;
  ColorOrder order_ = kDefaultColorOrder;
};

}  // namespace ledframe

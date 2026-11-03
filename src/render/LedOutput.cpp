#include "render/LedOutput.h"

#include <FastLED.h>

#include "core/Log.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "led";

// FastLED takes the pin as a template argument: one addLeds<> per pin in LedPins,
// and the one matching `pin` registers the strip. nullptr for any other pin.
template <uint8_t... Pins>
fl::CLEDController* addStrip(uint8_t pin, CRGB* leds, uint16_t count,
                             std::integer_sequence<uint8_t, Pins...>) {
  fl::CLEDController* controller = nullptr;
  ((pin == Pins && (controller = &FastLED.addLeds<WS2812B, Pins, RGB>(leds, count))) || ...);
  return controller;
}

}  // namespace

void LedOutput::begin(uint8_t pin) {
  // Registered with the full buffer: the first frame shrinks the count and switches
  // off every LED up to kMaxLeds, including ones left on before a restart.
  for (CRGB& led : leds_) {
    led = CRGB::Black;
  }
  if (!isValidLedPin(pin)) {
    LOG_W(kTag, "GPIO %u cannot drive the strip, using GPIO %u", static_cast<unsigned>(pin),
          static_cast<unsigned>(kDefaultLedPin));
    pin = kDefaultLedPin;
  }
  pin_ = pin;
  controller_ = addStrip(pin_, leds_, kMaxLeds, LedPins{});
  activeCount_ = kMaxLeds;
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setDither(BINARY_DITHER);
  setPowerLimit(kDefaultVolts, kDefaultMilliamps);
  LOG_I(kTag, "WS2812B on GPIO %u, color order %s", static_cast<unsigned>(pin_),
        colorOrderName(order_));
}

void LedOutput::show(const Frame& frame) {
  if (controller_ == nullptr) {
    return;
  }
  setActiveCount(frame.canvas.size());
  mapFrame(frame, order_, leds_);
  FastLED.setBrightness(frame.brightness);
  FastLED.show();
}

void LedOutput::showTestPattern(uint16_t count, uint8_t brightness) {
  if (controller_ == nullptr) {
    return;
  }
  if (count > kMaxLeds) {
    count = kMaxLeds;
  }
  setActiveCount(count);
  fillTestPattern(leds_, count);
  for (uint16_t i = 0; i < count; ++i) {
    leds_[i] = applyColorOrder(leds_[i], order_);
  }
  FastLED.setBrightness(brightness);
  FastLED.show();
}

bool LedOutput::setColorOrder(uint8_t value) {
  if (!isValidColorOrder(value)) {
    LOG_W(kTag, "invalid color order %u", static_cast<unsigned>(value));
    return false;
  }
  order_ = static_cast<ColorOrder>(value);
  LOG_I(kTag, "color order %s", colorOrderName(order_));
  return true;
}

void LedOutput::setPowerLimit(uint8_t volts, uint32_t milliamps) {
  if (milliamps == 0) {
    FastLED.setMaxPowerInMilliWatts(0xFFFFFFFFu);
    LOG_I(kTag, "power limit off");
    return;
  }
  FastLED.setMaxPowerInVoltsAndMilliamps(volts, milliamps);
  LOG_I(kTag, "power limit %u V, %u mA", static_cast<unsigned>(volts),
        static_cast<unsigned>(milliamps));
}

void LedOutput::setDither(bool on) {
  FastLED.setDither(on ? BINARY_DITHER : DISABLE_DITHER);
  LOG_I(kTag, "dithering %s", on ? "on" : "off");
}

void LedOutput::setActiveCount(uint16_t count) {
  if (count == activeCount_) {
    return;
  }
  if (count < activeCount_) {
    // Switch off the LEDs past the new end while FastLED still sends them.
    for (uint16_t i = count; i < activeCount_; ++i) {
      leds_[i] = CRGB::Black;
    }
    FastLED.show();
  }
  controller_->setLeds(leds_, count);
  activeCount_ = count;
}

}  // namespace ledframe

#include "render/LedOutput.h"

#include <FastLED.h>

#if defined(LEDFRAME_LED_CHECK)
#include <driver/pulse_cnt.h>
#include <esp_timer.h>
#endif

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
#if defined(LEDFRAME_LED_CHECK)
  checkFrame();  // The frame sent by the previous show() is out by now.
  const bool resized = frame.canvas.size() != activeCount_;
#endif
  setActiveCount(frame.canvas.size());
  mapFrame(frame, order_, leds_);
  FastLED.setBrightness(frame.brightness);
#if defined(LEDFRAME_LED_CHECK)
  int before = 0;
  if (counter_ != nullptr) {
    pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(counter_), &before);
  }
  lastCount_ = before;
  lastLeds_ = resized ? 0 : activeCount_;  // A resize sent an extra frame: not counted.
#endif
  FastLED.show();
#if defined(LEDFRAME_LED_CHECK)
  if (counter_ != nullptr && lastLeds_ > 0) {
    int after = 0;
    pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(counter_), &after);
    if (after - before < 24 * static_cast<int>(lastLeds_)) {
      ++earlyReturns_;
    }
  }
  logCheck(static_cast<uint32_t>(esp_timer_get_time() / 1000));
#endif
}

#if defined(LEDFRAME_LED_CHECK)

void LedOutput::checkFrame() {
  if (checkFailed_) {
    return;
  }
  if (counter_ == nullptr) {
    // Created after the first frame, once FastLED has set up the RMT output on the pin.
    // io_loop_back keeps the pin an output while the counter reads it.
    pcnt_unit_config_t unitConfig = {};
    unitConfig.low_limit = -1;
    unitConfig.high_limit = 30000;
    unitConfig.flags.accum_count = 1;
    pcnt_unit_handle_t unit = nullptr;
    pcnt_chan_config_t channelConfig = {};
    channelConfig.edge_gpio_num = pin_;
    channelConfig.level_gpio_num = -1;
    channelConfig.flags.io_loop_back = 1;
    pcnt_channel_handle_t channel = nullptr;
    if (pcnt_new_unit(&unitConfig, &unit) != ESP_OK ||
        pcnt_new_channel(unit, &channelConfig, &channel) != ESP_OK ||
        pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK ||
        pcnt_unit_add_watch_point(unit, unitConfig.high_limit) != ESP_OK ||
        pcnt_unit_enable(unit) != ESP_OK || pcnt_unit_clear_count(unit) != ESP_OK ||
        pcnt_unit_start(unit) != ESP_OK) {
      LOG_E(kTag, "LED check: cannot start the pulse counter on GPIO %u",
            static_cast<unsigned>(pin_));
      checkFailed_ = true;  // Frames still go out, without the check.
      lastLeds_ = 0;
      return;
    }
    counter_ = unit;
    LOG_I(kTag, "LED check: counting the edges on GPIO %u", static_cast<unsigned>(pin_));
    lastLeds_ = 0;
    return;
  }
  if (lastLeds_ == 0) {
    return;
  }
  int now = 0;
  pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(counter_), &now);
  const int edges = now - lastCount_;
  const int expected = 24 * static_cast<int>(lastLeds_);
  if (checkFrames_ == 0) {
    minEdges_ = edges;
    maxEdges_ = edges;
  }
  minEdges_ = edges < minEdges_ ? edges : minEdges_;
  maxEdges_ = edges > maxEdges_ ? edges : maxEdges_;
  ++checkFrames_;
  ++totalFrames_;
  if (edges != expected) {
    ++badFrames_;
    ++totalBad_;
  }
}

// Every 10 s: frames checked and corrupted ones since the last log, and in total.
void LedOutput::logCheck(uint32_t nowMs) {
  if (static_cast<int32_t>(nowMs - nextLogMs_) < 0) {
    return;
  }
  nextLogMs_ = nowMs + 10000;
  if (checkFrames_ == 0) {
    return;
  }
  LOG_I(kTag,
        "LED check: %lu frames, %lu bad (edges %d..%d, expected %d), total %lu bad of %lu, "
        "show() returned early %lu",
        static_cast<unsigned long>(checkFrames_), static_cast<unsigned long>(badFrames_),
        minEdges_, maxEdges_, 24 * static_cast<int>(activeCount_),
        static_cast<unsigned long>(totalBad_), static_cast<unsigned long>(totalFrames_),
        static_cast<unsigned long>(earlyReturns_));
  checkFrames_ = 0;
  badFrames_ = 0;
}

#endif  // LEDFRAME_LED_CHECK

void LedOutput::showTestPattern(uint16_t count, uint8_t brightness) {
  if (controller_ == nullptr) {
    return;
  }
  if (count > kMaxLeds) {
    count = kMaxLeds;
  }
  setActiveCount(count);
#if defined(LEDFRAME_LED_CHECK)
  lastLeds_ = 0;  // The test pattern is not checked; the next show() starts again.
#endif
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

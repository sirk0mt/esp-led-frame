// Candle: warm light that flickers slowly, like a candle flame; calmer than fire.
//
// The brightness follows Perlin noise (FastLED inoise8) in two octaves: a slow sway,
// 3/4 of the depth, and a faster flicker on top, 1/4. The flicker is 1D noise in time,
// one value for the whole frame. The sway is 2D noise over the frame that drifts up
// with time, so the light breathes over the frame unevenly, as warm air rising over
// a flame, instead of blinking as one lamp; neighbors stay alike. A dimmer moment is
// also a little redder, as a real flame gets when it shrinks. One 2D noise per pixel
// keeps render() near 0.5 ms; two 3D ones per pixel took about 1.5 ms on the ESP32.
//
// As in rain and starfield, the flicker runs on the light the eye sees (about
// the square root of the PWM level), the PWM level is its square, and levels are
// dithered in time, so slow dim moments do not step through the darkest levels.
//   intensity  how deep the flicker dims: 0 = steady light, 100 = down to a third
//   speed      how fast the flame flickers
//   color      flame color at full light (default: warm candle orange)
#include <lib8tion.h>
#include <noise.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kIntensity, kSpeed, kColor };

constexpr ParamSpec kParams[] = {
    {"intensity", "Intensity", ParamKind::Slider, 0, 100, 50},
    {"speed", "Speed", ParamKind::Slider, 1, 100, 30},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFF7A1E},
};

constexpr uint32_t kMaxDim = 170;  // seen light lost at full intensity, out of 256
constexpr uint16_t kSpread = 18;   // noise units per pixel: neighbors flicker alike

// 32-bit integer hash (lowbias32, as in twinkle).
uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Maps the usual inoise8 range 32..224 to 0..255 (as in noise).
uint32_t stretch(uint8_t noise) {
  const int32_t value = (static_cast<int32_t>(noise) - 32) * 4 / 3;
  return static_cast<uint32_t>(value < 0 ? 0 : value > 255 ? 255 : value);
}

class CandleEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    // Noise time: speed * 0.0256 noise units per ms, about 3 noise cells per second at
    // the default speed. From the 64-bit count before the cut to 16 bits: the noise
    // repeats every 65536 units, so the wrap is seamless.
    const uint64_t units = static_cast<uint64_t>(ctx.nowMs) *
                           static_cast<uint32_t>(ctx.params[kSpeed]) * 256u / 10000u;
    const uint16_t slow = static_cast<uint16_t>(units);
    const uint32_t flicker = stretch(inoise8(static_cast<uint16_t>(units * 3u)));
    const uint32_t depth = static_cast<uint32_t>(ctx.params[kIntensity]) * kMaxDim / 100u;
    const CRGB color = ctx.userColor(ctx.params[kColor]);
    // Dither noise of this frame: from the time, so the same time gives the same picture.
    const uint32_t frameNoise = hash(ctx.nowMs * 0x27D4EB2Du);

    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    CRGB* pixels = canvas.data();
    for (int i = 0; i < canvas.size(); ++i) {
      const uint16_t x = static_cast<uint16_t>((i % width) * kSpread);
      const uint16_t y = static_cast<uint16_t>((i / width) * kSpread);
      // Two octaves: 3/4 slow sway (drifting up), 1/4 frame-wide flicker; 0..255.
      const uint32_t sway = stretch(inoise8(x, static_cast<uint16_t>(y + slow)));
      const uint32_t n = (sway * 3u + flicker) / 4u;
      const uint32_t seen = 256u - depth * n / 255u;  // light the eye sees, 0..256
      const uint32_t level = seen * seen;             // PWM level, 0..65536
      const uint32_t noise = hash(frameNoise ^ (static_cast<uint32_t>(i) * 0x9E3779B9u)) & 0xFFu;
      // Dimmer is redder: green and blue lose another seen/256 on top.
      const uint32_t cool = level * seen / 256u;
      auto channel = [noise](uint8_t c, uint32_t scale) {
        const uint32_t value = ((c * scale) >> 8) + noise;  // 8.8 fixed point
        return static_cast<uint8_t>(value >> 8 > 255u ? 255u : value >> 8);
      };
      pixels[i] = CRGB(channel(color.r, level), channel(color.g, cool), channel(color.b, cool));
    }
    return true;
  }
};

}  // namespace

REGISTER_EFFECT(CandleEffect, "candle", "Candle", 12, 0, kParams);

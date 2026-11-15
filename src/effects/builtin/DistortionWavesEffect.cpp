// Distortion waves: red, green and blue waves bent by ripples from three wandering
// centers.
//
// Adapted from "Distortion Waves" in WLED (wled00/FX.cpp, mode_2Ddistortionwaves; by
// ldirko, https://editor.soulmatelights.com/gallery/1089-distorsion-waves, adapted for
// WLED by blazoncek, improvements by dedehai). Written again for this firmware; the
// picture and the options follow WLED.
//
// Every color channel is a cosine of two parts, both moving with time a:
//   - a distortion: a cosine of the sum of a cosine along x and a cosine along y
//     (alt: one cosine along the diagonal)
//   - rings: the squared distance from the channel's own center, which wanders over
//     the matrix on two sine waves. The rings run outwards as a grows.
// Red moves at a, green at a/2 and blue at a/3, so the colors drift apart and mix.
// With a palette, the colors are taken from the palette by the hue of the channels
// (palette) or by their brightness (fill).
//
// The picture is a function of time only, so it looks the same at any FPS. Unlike WLED,
// which counts time in steps of 32 ms and angles in 1/256 turns, time runs on in
// fractions and the angles have 16 bits (cos16), and the centers move in fractions of a
// step. At low speed WLED's picture changes in visible steps; here it flows.
//   speed     pace of the whole picture, in tenths of WLED's (10: as WLED with the
//             default speed, 0: still). WLED's speed changes only the pace of the
//             centers, and inversely; here they keep WLED's default rates
//   scale     size of the rings: higher gives more, narrower rings
//   zoom      on: adds 192 / (width + height) to the scale (zooms out)
//   alt       on: the distortion runs along the diagonal (the original alternative)
//   colors    rgb: the channels as they are (WLED's default palette); palette: the
//             palette by the hue of the channels, smeared to hide where the palette
//             wraps; fill: the palette by their brightness
//   palette   colors for colors = palette or fill
#include <lib8tion.h>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kScale, kZoom, kAlt, kColors, kPalette };
enum Colors : int32_t { kColorsRgb, kColorsPalette, kColorsFill };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 30, 10},
    {"scale", "Scale", ParamKind::Slider, 0, 7, 4},
    {"zoom", "Zoom", ParamKind::Toggle, 0, 1, 0},
    {"alt", "Alt", ParamKind::Toggle, 0, 1, 0},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsRgb, "rgb|palette|fill"},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

// Rates of the centers in beats per minute, x and y: WLED's 10, 12 / 13, 15 / 17, 14
// minus its default speed (128 / 32 = 4).
constexpr uint32_t kCenterRates[3][2] = {{6, 8}, {9, 11}, {13, 10}};
constexpr uint8_t kSmearAmount = 200;  // WLED's blur over the hue-mapped palette
constexpr uint32_t kMaxStepMs = 1000;  // a long stall moves the picture at most 1 s on

// cos as 0..65535, for angles in 1/65536 turns: cos8 of WLED times 256.
inline uint32_t cosine(uint32_t angle) {
  return static_cast<uint32_t>(cos16(static_cast<uint16_t>(angle)) + 32768);
}

// Hue 0..255 of a color (red 0, green 85, blue 170), as WLED's rgb2hsv.
uint8_t hueOf(int r, int g, int b) {
  const int top = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const int bottom = r < g ? (r < b ? r : b) : (g < b ? g : b);
  const int delta = top - bottom;
  if (delta == 0) {
    return 0;  // gray: no hue
  }
  int hue;  // 1/65536 turns
  if (top == r) {
    hue = 10923 * (g - b) / delta;
  } else if (top == g) {
    hue = 21845 + 10923 * (b - r) / delta;
  } else {
    hue = 43690 + 10923 * (r - g) / delta;
  }
  return static_cast<uint8_t>(static_cast<uint16_t>(hue) >> 8);
}

class DistortionWavesEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override {
    clock_ = 0;
    dirty_ = true;
  }

  void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) override { dirty_ = true; }

  bool render(FrameContext& ctx) override {
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    if (speed == 0 && !dirty_) {
      return false;  // Still picture: nothing changed since the last frame.
    }
    dirty_ = false;
    const uint32_t deltaMs = ctx.deltaMs < kMaxStepMs ? ctx.deltaMs : kMaxStepMs;
    clock_ += static_cast<uint64_t>(deltaMs) * speed;  // WLED time in 1/10 ms
    draw(ctx);
    return true;
  }

 private:
  void draw(FrameContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    int scaleInt = ctx.params[kScale];
    if (ctx.params[kZoom] != 0) {
      scaleInt += 192 / (width + height);
    }
    const float scale = static_cast<float>(scaleInt);

    // WLED: a = now / 32, a2 = a / 2, a3 = a / 3, used as angles in 1/256 turns. Here in
    // 1/65536 turns with the fractions: a = now * 8, from the full 64-bit clock (1/10 ms)
    // so that a / 2 and a / 3 do not jump when a wraps.
    const uint32_t a = static_cast<uint32_t>(clock_ * 8 / 10);
    const uint32_t a2 = static_cast<uint32_t>(clock_ * 8 / 20);
    const uint32_t a3 = static_cast<uint32_t>(clock_ * 8 / 30);

    // Centers over 0..width * scale and 0..height * scale, as WLED's beatsin16 on the
    // same clock: beat = now * bpm * 256 * 280 / 65536, now = clock / 10.
    float centerX[3];
    float centerY[3];
    for (int c = 0; c < 3; ++c) {
      centerX[c] = wave(kCenterRates[c][0]) * static_cast<float>(width) * scale;
      centerY[c] = wave(kCenterRates[c][1]) * static_cast<float>(height) * scale;
    }

    const bool alt = ctx.params[kAlt] != 0;
    const int32_t colors = ctx.params[kColors];
    CRGB* pixels = canvas.data();
    for (int x = 0; x < width; ++x) {
      const uint32_t ax = static_cast<uint32_t>(x) * 2048u;  // x * 8 in 1/256 turns
      const uint32_t xr = cosine(ax + a);
      const uint32_t xg = cosine(ax - a2);
      const uint32_t xb = cosine(ax + a3);
      const float offsetX = static_cast<float>(x + 1) * scale;
      for (int y = 0; y < height; ++y) {
        const uint32_t ay = static_cast<uint32_t>(y) * 2048u;
        uint32_t distortR;
        uint32_t distortG;
        uint32_t distortB;
        if (alt) {
          const uint32_t diagonal = ax + ay;
          distortR = cosine(diagonal + a2) >> 1;
          distortG = cosine(diagonal + a3 + 8192u) >> 1;
          distortB = cosine(diagonal + a + 16384u) >> 1;
        } else {
          distortR = cosine(xr + cosine(ay - a2) + a3) >> 1;
          distortG = cosine(xg + cosine(ay + a3) + a + 8192u) >> 1;
          distortB = cosine(xb + cosine(ay - a) + a2 + 16384u) >> 1;
        }

        // WLED: value = distort + ((a - d^2 / 128) << 1), d^2 in scaled units; in 1/65536
        // turns 2 * a - 4 * d^2.
        const float offsetY = static_cast<float>(y + 1) * scale;
        const uint8_t r = channel(distortR, a, offsetX, offsetY, centerX[0], centerY[0]);
        const uint8_t g = channel(distortG, a2, offsetX, offsetY, centerX[1], centerY[1]);
        const uint8_t b = channel(distortB, a3, offsetX, offsetY, centerX[2], centerY[2]);

        CRGB& pixel = pixels[y * width + x];
        if (colors == kColorsRgb) {
          pixel = CRGB(r, g, b);
        } else {
          const uint8_t brightness = static_cast<uint8_t>((r + g + b) / 3);
          pixel = colors == kColorsFill
                      ? ColorFromPalette(ctx.palette, brightness, 255, LINEARBLEND_NOWRAP)
                      : ColorFromPalette(ctx.palette, hueOf(r, g, b), brightness, LINEARBLEND);
        }
      }
    }

    if (colors == kColorsPalette) {
      canvas.blur(kSmearAmount, true);
    }
  }

  // Sine wave 0..1 at the given rate on the effect clock (WLED beatsin16).
  float wave(uint32_t bpm) const {
    const uint16_t beat = static_cast<uint16_t>((clock_ * bpm * 7168u) >> 16);
    return static_cast<float>(static_cast<int32_t>(sin16(beat)) + 32768) / 65535.0f;
  }

  static uint8_t channel(uint32_t distort, uint32_t time, float x, float y, float centerX,
                         float centerY) {
    const float dx = x - centerX;
    const float dy = y - centerY;
    // Through int: float to int is one instruction, float to unsigned is a call.
    const uint32_t rings = static_cast<uint32_t>(static_cast<int32_t>(4.0f * (dx * dx + dy * dy)));
    return static_cast<uint8_t>(cosine(distort + 2u * time - rings) >> 8);
  }

  uint64_t clock_ = 0;  // WLED time in 1/10 ms: deltaMs * speed, speed 10 = real time
  bool dirty_ = true;
};

}  // namespace

REGISTER_EFFECT(DistortionWavesEffect, "distortion-waves", "Distortion waves", 53,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

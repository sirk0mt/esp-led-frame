// Black hole: stars circling a white center, leaving fading trails.
//
// Adapted from "Black Hole" in WLED (wled00/FX.cpp, mode_2DBlackHole; by Stepko,
// https://editor.soulmatelights.com/gallery/1012, modified by Andrew Tuline). Written
// again for this firmware; the motion, the colors and the options follow WLED.
//
// Each coordinate of a star swings along its axis on a sine wave. With equal rates for
// x and y a quarter turn apart, a star runs along an ellipse around the center:
//   - 8 outer stars over the whole matrix, colored from the palette at 0, 32 .. 224
//   - 4 inner stars over the middle half of the matrix, at 255, 191, 127, 63
// Even and odd stars start half a turn apart, and the phase of star i grows with time
// at i / 32 turns per second, so the stars spread out and gather again. One simulation
// step (one frame in WLED, about 42 per second) dims the whole matrix (the trails),
// draws the stars and the white center, and optionally blurs the matrix. Steps run at
// a fixed rate counted from deltaMs, and the stars follow the step time, so the effect
// looks the same at any FPS.
//
// Unlike WLED, which moves the stars from pixel to pixel, the stars are kept in
// fractions of a pixel and drawn as soft dots: a pixel gets light by its distance from
// the star, full at the star and none at the size. On a small matrix a star therefore
// glides across the pixels instead of jumping. The dot falls off as (1 - d^2/r^2) in
// perceived brightness, squared for the LEDs (docs/ADDING_EFFECT.md, section 5). Stars
// lighten the pixels instead of adding to them: added every step, the faint edge of a
// slow star would build up to full light. WLED also moves the phases in whole steps
// every 128 ms, a visible jerk on a small matrix; here they grow smoothly. The white
// center is the middle of the matrix, where the stars circle: WLED's single pixel at
// (width / 2, height / 2) is off the middle on an even side, so here both pixels
// around the middle are white (one pixel on an odd side, two or four on even sides).
//   fade          trail fade per step: (16 + fade) / 256
//   outer_x       rate of the outer stars along x, beats per minute
//   outer_y       the same along y
//   inner_x       rate of the inner stars along x
//   inner_y       the same along y
//   size          star radius, in tenths of a pixel
//   colors        palette: stars colored from the palette; color: every star in color
//   color         star color for colors = color (WLED's default primary color)
//   blur          on: blurs the matrix a little every step (smears below 100 pixels)
//   palette       star colors
#include <lib8tion.h>

#include <cmath>

#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t {
  kFade,
  kOuterX,
  kOuterY,
  kInnerX,
  kInnerY,
  kSize,
  kColors,
  kColor,
  kBlur,
  kPalette
};
enum Colors : int32_t { kColorsPalette, kColorsColor };

constexpr ParamSpec kParams[] = {
    {"fade", "Fade", ParamKind::Slider, 0, 31, 16},
    {"outer_x", "Outer X freq", ParamKind::Slider, 0, 31, 16},
    {"outer_y", "Outer Y freq", ParamKind::Slider, 0, 31, 16},
    {"inner_x", "Inner X freq", ParamKind::Slider, 0, 31, 16},
    {"inner_y", "Inner Y freq", ParamKind::Slider, 0, 31, 16},
    {"size", "Size", ParamKind::Slider, 10, 25, 12},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsPalette, "palette|color"},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFA000},
    {"blur", "Blur", ParamKind::Toggle, 0, 1, 0},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kStepMs = 24;          // about 42 steps per second, the WLED frame rate
constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall the stars skip ahead, drawn once
constexpr uint8_t kBaseFade = 16;         // trail fade per step, out of 256, plus fade
constexpr uint8_t kBlurAmount = 16;
constexpr int kOuterStars = 8;
constexpr int kInnerStars = 4;
constexpr int kSmearBelowPixels = 100;  // WLED smears the blur on matrices this small
const CRGB kCenter(255, 255, 255);

// Dot level for the LEDs by the squared distance from a star: entry i is for
// d^2/r^2 = i/256, (1 - i/256)^2 * 255. Perceived brightness falls off as 1 - d^2/r^2
// and is squared for the LEDs (docs/ADDING_EFFECT.md, section 5).
struct GlowTable {
  uint8_t level[256];
};

constexpr GlowTable makeGlowTable() {
  GlowTable table{};
  for (int i = 0; i < 256; ++i) {
    const float perceived = 1.0f - static_cast<float>(i) / 256.0f;
    table.level[i] = static_cast<uint8_t>(perceived * perceived * 255.0f + 0.5f);
  }
  return table;
}

constexpr GlowTable kGlow = makeGlowTable();

// Sine wave over low..high (pixel centers) with the given rate, as FastLED beatsin16 but
// on the step clock and in fractions of a pixel. phase is in 1/65536 turns. The beat is
// 64-bit: timeMs * bpm * 256 * 280 overflows 32 bits after a few seconds; only its low
// 16 bits (one turn) matter.
float swing(uint32_t timeMs, uint32_t bpm, uint16_t phase, int low, int high) {
  const uint32_t beat16 =
      static_cast<uint32_t>((static_cast<uint64_t>(timeMs) * bpm * 256u * 280u) >> 16) & 0xFFFFu;
  const uint16_t angle = static_cast<uint16_t>(beat16 + phase);
  const int32_t wave = static_cast<int32_t>(sin16(angle)) + 32768;  // 1 .. 65535
  return static_cast<float>(low) +
         static_cast<float>(wave) * static_cast<float>(high - low) / 65535.0f;
}

// Lightens a pixel to weight/256 of a color, channel by channel, so a star drawn again
// every step keeps its own soft edge. The helpers are inline and work on single
// channels: the esp32dev firmware is built with -Og, which does not inline small
// functions on its own.
inline void lighten(CRGB& pixel, const CRGB& color, uint8_t weight) {
  const uint8_t r = scale8(color.r, weight);
  const uint8_t g = scale8(color.g, weight);
  const uint8_t b = scale8(color.b, weight);
  pixel.r = pixel.r > r ? pixel.r : r;
  pixel.g = pixel.g > g ? pixel.g : g;
  pixel.b = pixel.b > b ? pixel.b : b;
}

inline int clampIndex(int value, int size) {
  return value < 0 ? 0 : (value >= size ? size - 1 : value);
}

// Draws a soft dot at (x, y): every pixel within radius gets the color at
// (1 - d^2/r^2)^2 of it, d being its distance from the dot.
void drawDot(Canvas& canvas, float x, float y, const CRGB& color, float radius) {
  const int width = canvas.width();
  const float radius2 = radius * radius;
  const float glowScale = 256.0f / radius2;  // d^2 to an index of kGlow
  // Through int: float to int is one instruction, float to unsigned is a call.
  const int left = clampIndex(static_cast<int>(std::floor(x - radius)), width);
  const int right = clampIndex(static_cast<int>(std::ceil(x + radius)), width);
  const int top = clampIndex(static_cast<int>(std::floor(y - radius)), canvas.height());
  const int bottom = clampIndex(static_cast<int>(std::ceil(y + radius)), canvas.height());
  for (int py = top; py <= bottom; ++py) {
    const float dy = static_cast<float>(py) - y;
    const float dy2 = dy * dy;
    CRGB* row = canvas.data() + py * width;
    for (int px = left; px <= right; ++px) {
      const float dx = static_cast<float>(px) - x;
      const float distance2 = dx * dx + dy2;
      if (distance2 >= radius2) {
        continue;
      }
      // The product may round up to 256 just below the radius.
      const int index = static_cast<int>(distance2 * glowScale);
      lighten(row[px], color, kGlow.level[index < 255 ? index : 255]);
    }
  }
}

// One blur pass along a row or a column (FastLED blur1d, as WLED blur2D): every pixel
// keeps keep/256 of itself and passes seep/256 to each neighbor. With smear it keeps
// all of itself. Works on one color channel at a time: the bytes of the channel lie
// 3 * stride apart (CRGB is r, g, b).
void blurLine(CRGB* first, int count, int stride, uint8_t keep, uint8_t seep) {
  const int step = 3 * stride;
  for (int channel = 0; channel < 3; ++channel) {
    uint8_t* value = &first->raw[channel];
    uint8_t carry = 0;
    for (int i = 0; i < count; ++i, value += step) {
      const uint8_t part = scale8(*value, seep);
      *value = qadd8(scale8(*value, keep), carry);
      if (i > 0) {
        value[-step] = qadd8(value[-step], part);
      }
      carry = part;
    }
  }
}

void blur(Canvas& canvas, uint8_t amount, bool smear) {
  const uint8_t keep = smear ? 255 : static_cast<uint8_t>(255 - amount);
  const uint8_t seep = amount >> 1;
  CRGB* pixels = canvas.data();
  const int width = canvas.width();
  const int height = canvas.height();
  for (int y = 0; y < height; ++y) {
    blurLine(pixels + y * width, width, 1, keep, seep);
  }
  for (int x = 0; x < width; ++x) {
    blurLine(pixels + x, height, width, keep, seep);
  }
}

class BlackHoleEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    stepTimeMs_ = ctx.nowMs;
    carryMs_ = 0;
  }

  bool render(FrameContext& ctx) override {
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      stepTimeMs_ += kStepMs;
      if (steps < kMaxStepsPerFrame) {
        step(ctx);
        ++steps;
      }
    }
    return steps > 0;  // no step: the same picture as in the last frame
  }

 private:
  void step(FrameContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    const float radius = static_cast<float>(ctx.params[kSize]) / 10.0f;
    const bool oneColor = ctx.params[kColors] == kColorsColor;
    const CRGB userColor = ctx.userColor(ctx.params[kColor]);

    canvas.fadeToBlackBy(static_cast<uint8_t>(kBaseFade + ctx.params[kFade]));

    // WLED: phase (now / 128) * i in 1/256 turns. Here the same drift in 1/65536 turns,
    // without the steps: now * 2 * i.
    const uint32_t drift = stepTimeMs_ * 2u;
    const uint32_t outerX = static_cast<uint32_t>(ctx.params[kOuterX]);
    const uint32_t outerY = static_cast<uint32_t>(ctx.params[kOuterY]);
    for (int i = 0; i < kOuterStars; ++i) {
      const uint32_t phase = drift * static_cast<uint32_t>(i);
      const uint16_t phaseX = static_cast<uint16_t>((i % 2 ? 128u << 8 : 0u) + phase);
      const uint16_t phaseY = static_cast<uint16_t>((i % 2 ? 192u << 8 : 64u << 8) + phase);
      const CRGB color =
          oneColor ? userColor
                   : ColorFromPalette(ctx.palette, static_cast<uint8_t>(i * 32), 255, LINEARBLEND);
      drawDot(canvas, swing(stepTimeMs_, outerX, phaseX, 0, width - 1),
              swing(stepTimeMs_, outerY, phaseY, 0, height - 1), color, radius);
    }

    const uint32_t innerX = static_cast<uint32_t>(ctx.params[kInnerX]);
    const uint32_t innerY = static_cast<uint32_t>(ctx.params[kInnerY]);
    for (int i = 0; i < kInnerStars; ++i) {
      const uint32_t phase = drift * static_cast<uint32_t>(i);
      const uint16_t phaseX = static_cast<uint16_t>((i % 2 ? 128u << 8 : 0u) + phase);
      const uint16_t phaseY = static_cast<uint16_t>((i % 2 ? 192u << 8 : 64u << 8) + phase);
      const CRGB color =
          oneColor
              ? userColor
              : ColorFromPalette(ctx.palette, static_cast<uint8_t>(255 - i * 64), 255, LINEARBLEND);
      drawDot(canvas, swing(stepTimeMs_, innerX, phaseX, width / 4, width - 1 - width / 4),
              swing(stepTimeMs_, innerY, phaseY, height / 4, height - 1 - height / 4), color,
              radius);
    }

    // The center stands still, so it is drawn sharp, at full white: the pixel in the
    // middle, or the two (four) around it on an even side, as WLED's single pixel.
    for (int y = (height - 1) / 2; y <= height / 2; ++y) {
      for (int x = (width - 1) / 2; x <= width / 2; ++x) {
        canvas.at(x, y) = kCenter;
      }
    }

    if (ctx.params[kBlur] != 0) {
      blur(canvas, kBlurAmount, width * height < kSmearBelowPixels);
    }
  }

  uint32_t stepTimeMs_ = 0;  // clock of the stars: kStepMs per step, from begin()
  uint32_t carryMs_ = 0;
};

}  // namespace

REGISTER_EFFECT(BlackHoleEffect, "black-hole", "Black hole", 62,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

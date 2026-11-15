// Colored bursts: soft lines from the palette whose ends swing across the matrix,
// leaving fading trails.
//
// Adapted from "Colored Bursts" in WLED (wled00/FX.cpp, mode_2DColoredBursts; by
// ldirko, https://editor.soulmatelights.com/gallery/819-colored-bursts, modified by
// Andrew Tuline). Written again for this firmware; the motion, the colors and the
// options follow WLED.
//
// Every line runs from a start point to an end point. Each coordinate of both points
// swings along its axis on a sine wave with its own rate in beats per minute: start x
// 2, start y 1, end x 5, end y 3, all plus the speed. All start points move together;
// the end points of the lines are spread by a phase offset, which gives the fan of
// lines. One simulation step (one frame in WLED, about 42 per second):
//   - dims the whole matrix (the trails)
//   - draws every line, colored from the palette: the lines share the palette between
//     them, and the whole set moves one palette step on
//   - optionally marks the line ends (dots)
//   - blurs the matrix a little
// Steps run at a fixed rate counted from deltaMs, and the line ends follow the step
// time, so the effect looks the same at any FPS.
//
// Unlike WLED, which moves the ends from pixel to pixel and adds one pixel per step
// along the line, the ends are kept in fractions of a pixel and every line is drawn
// anti-aliased with a soft glow: a pixel gets light by its distance from the line,
// full on the line and none at the width. On a small matrix a slow line therefore
// glides across the pixels instead of jumping. The glow falls off as (1 - d^2/w^2) in
// perceived brightness, squared for the LEDs (docs/ADDING_EFFECT.md, section 5). Lines
// lighten the pixels instead of adding to them: added every step, even a faint glow
// would build up to full light and fill a small matrix.
//   speed     beats per minute added to the base rates of the line ends (2, 1, 5, 3)
//   lines     number of lines
//   width     glow radius around every line, in tenths of a pixel (1.0-3.0 px; at 4 px
//             every line lights most of a 15 x 10 matrix, and 16 of them took up to
//             2.1 ms a frame on the ESP32)
//   blur      blur strength per step, out of 255 (0 = no blur)
//   gradient  on: every line is dark at its start and full at its end
//   smear     on: blur keeps the full pixel and only adds to its neighbors, and the
//             trails fade slower, so the picture is brighter and more smeared
//   dots      on: a white dot at the start of every line and a dark gray one at its end
//   palette   line colors
#include <lib8tion.h>

#include <cmath>

#include "effects/EffectRegistry.h"
#include "render/SoftDraw.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::glowLevel;
using ledframe::lighten;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kLines, kWidth, kBlur, kGradient, kSmear, kDots, kPalette };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 15, 8},
    {"lines", "Lines", ParamKind::Slider, 1, 16, 9},
    {"width", "Width", ParamKind::Slider, 10, 30, 16},
    {"blur", "Blur", ParamKind::Slider, 0, 15, 8},
    {"gradient", "Gradient", ParamKind::Toggle, 0, 1, 0},
    {"smear", "Smear", ParamKind::Toggle, 0, 1, 0},
    {"dots", "Dots", ParamKind::Toggle, 0, 1, 0},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kStepMs = 24;          // about 42 steps per second, the WLED frame rate
constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall the lines skip ahead, drawn once
constexpr uint8_t kFade = 40;             // trail fade per step, out of 256
constexpr uint8_t kSmearFade = 32;        // the same with smear
const CRGB kStartDot(255, 255, 255);      // white
const CRGB kEndDot(0x2F, 0x4F, 0x4F);     // dark slate gray

struct Point {
  float x;
  float y;
};

// Sine wave over 0..length-1 (pixel centers) with the given rate, as FastLED beatsin16
// but on the step clock and in fractions of a pixel. The beat is 64-bit: timeMs * bpm *
// 256 * 280 overflows 32 bits after a few seconds; only its low 16 bits (one turn)
// matter. The phase is in 1/256 turns, as in WLED.
float swing(uint32_t timeMs, uint32_t bpm, uint8_t phase, int length) {
  const uint32_t beat16 =
      static_cast<uint32_t>((static_cast<uint64_t>(timeMs) * bpm * 256u * 280u) >> 16) & 0xFFFFu;
  const uint16_t angle = static_cast<uint16_t>(beat16 + (static_cast<uint32_t>(phase) << 8));
  const int32_t wave = static_cast<int32_t>(sin16(angle)) + 32768;  // 1 .. 65535
  return static_cast<float>(wave) * static_cast<float>(length - 1) / 65535.0f;
}

inline int clampIndex(float value, int size) {
  const int i = static_cast<int>(value);
  return i < 0 ? 0 : (i >= size ? size - 1 : i);
}

// Draws a soft line from a to b: every pixel within radius of the segment is lightened
// to the color at (1 - d^2/r^2)^2 of it, d being its distance from the segment. With
// gradient the level also grows from 0 at a to full at b, as t^2 in the LED value.
//
// Only the pixels that can be that close are visited: the rows of the bounding box of
// the glow, and in every row the stretch of the band of half-width radius around the
// infinite line through a and b. A long slanted line would otherwise visit nearly the
// whole matrix for a few pixels of glow per row.
//
// Per pixel the work is kept small, since a wide glow covers most of the matrix for
// every line: along a row the distance from the line (cross / length) and the position
// t on it grow by a constant, the end points are needed only past the ends, and the
// glow level comes from a table by d^2/r^2.
void drawSoftLine(Canvas& canvas, Point a, Point b, const CRGB& color, float radius,
                  bool gradient) {
  const int width = canvas.width();
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length2 = dx * dx + dy * dy;
  const float invLength2 = length2 > 0.0f ? 1.0f / length2 : 0.0f;
  const float radius2 = radius * radius;
  const float glowScale = 256.0f / radius2;  // d^2 to an index of kGlow
  const int left = clampIndex(std::floor(std::fmin(a.x, b.x) - radius), width);
  const int right = clampIndex(std::ceil(std::fmax(a.x, b.x) + radius), width);
  const int top = clampIndex(std::floor(std::fmin(a.y, b.y) - radius), canvas.height());
  const int bottom = clampIndex(std::ceil(std::fmax(a.y, b.y) + radius), canvas.height());
  // Band: a pixel at distance d from the line is |x - lineX(y)| * |dy| / length from it,
  // so in a row only |x - lineX(y)| < radius * length / |dy| can be in the glow. A
  // nearly level line crosses the rows of its bounding box only, so its band is the
  // whole row.
  const bool slanted = std::fabs(dy) > 0.01f;
  const float slope = slanted ? dx / dy : 0.0f;
  const float halfBand = slanted ? radius * std::sqrt(length2) / std::fabs(dy) : 0.0f;
  const float tStep = dx * invLength2;  // change of t from one pixel to the next in a row
  for (int y = top; y <= bottom; ++y) {
    const float wy = static_cast<float>(y) - a.y;
    int from = left;
    int to = right;
    if (slanted) {
      const float lineX = a.x + wy * slope;
      const int bandFrom = static_cast<int>(std::ceil(lineX - halfBand));
      const int bandTo = static_cast<int>(std::floor(lineX + halfBand));
      from = bandFrom > from ? bandFrom : from;
      to = bandTo < to ? bandTo : to;
    }
    CRGB* row = canvas.data() + y * width;
    const float startY2 = wy * wy;              // squared y distance from a
    const float endY2 = (wy - dy) * (wy - dy);  // and from b
    float wx = static_cast<float>(from) - a.x;
    float tLine = (wx * dx + wy * dy) * invLength2;  // nearest point: a + t * (b - a)
    float cross = wx * dy - wy * dx;                 // distance from the line * length
    for (int x = from; x <= to; ++x, wx += 1.0f, tLine += tStep, cross += dy) {
      float distance2;
      float t;
      if (tLine <= 0.0f) {
        distance2 = wx * wx + startY2;
        t = 0.0f;
      } else if (tLine >= 1.0f) {
        distance2 = (wx - dx) * (wx - dx) + endY2;
        t = 1.0f;
      } else {
        distance2 = cross * cross * invLength2;
        t = tLine;
      }
      if (distance2 >= radius2) {
        continue;
      }
      // Through int: float to int is one instruction, float to unsigned is a call.
      uint8_t weight = glowLevel(static_cast<int>(distance2 * glowScale));
      if (gradient) {
        weight = scale8(weight, static_cast<uint8_t>(static_cast<int>(t * t * 255.0f)));
      }
      if (weight != 0) {
        lighten(row[x], color, weight);
      }
    }
  }
}

// Puts a dot at a point between pixels: it covers the 4 nearest pixels by their share
// of the point (bilinear), so a moving dot glides like the lines.
void drawDot(Canvas& canvas, Point p, const CRGB& color) {
  const int x0 = static_cast<int>(std::floor(p.x));
  const int y0 = static_cast<int>(std::floor(p.y));
  const float fx = p.x - static_cast<float>(x0);
  const float fy = p.y - static_cast<float>(y0);
  const float shares[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
  for (int i = 0; i < 4; ++i) {
    const int x = x0 + (i & 1);
    const int y = y0 + (i >> 1);
    const uint8_t amount = static_cast<uint8_t>(shares[i] * 255.0f + 0.5f);
    if (amount != 0 && canvas.contains(x, y)) {
      nblend(canvas.at(x, y), color, amount);
    }
  }
}

class ColoredBurstsEffect : public Effect {
 public:
  void begin(FrameContext& ctx) override {
    stepTimeMs_ = ctx.nowMs;
    carryMs_ = 0;
    hue_ = 0;  // starts at the first palette color, as in WLED
  }

  bool render(FrameContext& ctx) override {
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      stepTimeMs_ += kStepMs;
      ++hue_;
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
    const uint32_t speed = static_cast<uint32_t>(ctx.params[kSpeed]);
    const int lines = ctx.params[kLines];
    const float radius = static_cast<float>(ctx.params[kWidth]) / 10.0f;
    const bool smear = ctx.params[kSmear] != 0;
    const bool gradient = ctx.params[kGradient] != 0;
    const bool dots = ctx.params[kDots] != 0;

    canvas.fadeToBlackBy(smear ? kSmearFade : kFade);

    // The start point is the same for every line; only the end points have a phase.
    const Point start{swing(stepTimeMs_, 2 + speed, 0, width),
                      swing(stepTimeMs_, 1 + speed, 0, height)};
    for (int line = 0; line < lines; ++line) {
      const Point end{swing(stepTimeMs_, 5 + speed, static_cast<uint8_t>(line * 24), width),
                      swing(stepTimeMs_, 3 + speed, static_cast<uint8_t>(line * 48 + 64), height)};
      const uint8_t index = static_cast<uint8_t>(line * 255 / lines + hue_);
      const CRGB color = ColorFromPalette(ctx.palette, index, 255, LINEARBLEND);
      drawSoftLine(canvas, start, end, color, radius, gradient);
      if (dots) {
        drawDot(canvas, start, kStartDot);
        drawDot(canvas, end, kEndDot);
      }
    }

    canvas.blur(static_cast<uint8_t>(ctx.params[kBlur]), smear);
  }

  uint32_t stepTimeMs_ = 0;  // clock of the line ends: kStepMs per step, from begin()
  uint32_t carryMs_ = 0;
  uint8_t hue_ = 0;  // palette shift, one step per simulation step
};

}  // namespace

REGISTER_EFFECT(ColoredBurstsEffect, "colored-bursts", "Colored bursts", 59,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);

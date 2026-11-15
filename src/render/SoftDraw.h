// Soft drawing for effects that move points and lines in fractions of a pixel.
//
// On a small matrix a point drawn into its nearest pixel jumps from pixel to pixel. A
// soft point lights the pixels around it by their distance d from it, full at the point
// and none at the radius r: perceived brightness falls off as 1 - d^2/r^2 and is squared
// for the LEDs (docs/ADDING_EFFECT.md, section 5), so a moving point glides across the
// pixels.
//
// Soft shapes lighten the pixels instead of adding to them: a shape drawn again every
// step keeps its own soft edge, where added light would build up to full brightness.
// Crossing shapes still mix (red and blue give magenta).
//
// The helpers are inline and work on single channels: the esp32dev firmware is built
// with -Og, which does not inline small functions on its own, and calls with CRGB
// copies per pixel doubled the time of an effect step.
#pragma once

#include <crgb.h>
#include <lib8tion.h>

#include <cstdint>

#include "render/Canvas.h"

namespace ledframe {

// Glow level for the LEDs by the squared distance from a shape: entry i is for
// d^2/r^2 = i/256, (1 - i/256)^2 * 255.
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

inline constexpr GlowTable kGlow = makeGlowTable();

// Glow level for d^2 * 256 / r^2, any value below 256 * 256. The product may round up
// to 256 just below the radius, so the index stops at 255.
inline uint8_t glowLevel(int index) {
  return kGlow.level[index < 255 ? index : 255];
}

// Lightens a pixel to weight/256 of a color, channel by channel.
inline void lighten(CRGB& pixel, const CRGB& color, uint8_t weight) {
  const uint8_t r = scale8(color.r, weight);
  const uint8_t g = scale8(color.g, weight);
  const uint8_t b = scale8(color.b, weight);
  pixel.r = pixel.r > r ? pixel.r : r;
  pixel.g = pixel.g > g ? pixel.g : g;
  pixel.b = pixel.b > b ? pixel.b : b;
}

// Draws a soft point at (x, y), in pixels of the canvas (pixel centers are whole
// numbers): every pixel closer than radius is lightened to the glow level of the color.
// Parts outside the canvas are cut off.
void drawSoftDot(Canvas& canvas, float x, float y, const CRGB& color, float radius);

}  // namespace ledframe

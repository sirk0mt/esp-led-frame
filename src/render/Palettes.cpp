#include "render/Palettes.h"

#include <colorpalettes.h>

#include <cstring>

#include "render/Gamma.h"

namespace ledframe {

namespace {

struct PaletteInfo {
  const char* id;
  const char* name;
};

// Order defines the palette index. kUserIndex must point at "custom".
constexpr PaletteInfo kInfo[Palettes::kCount] = {
    {"rainbow", "Rainbow"}, {"party", "Party"}, {"ocean", "Ocean"}, {"forest", "Forest"},
    {"lava", "Lava"},       {"heat", "Heat"},   {"cloud", "Cloud"}, {"custom", "Custom"},
};

constexpr uint8_t kEntries = 16;  // entries in a CRGBPalette16

// Linear interpolation with rounding: t = 0 gives a, t = 255 gives b exactly.
uint8_t lerpChannel(uint8_t a, uint8_t b, uint8_t t) {
  return static_cast<uint8_t>((a * (255u - t) + b * static_cast<unsigned>(t) + 127u) / 255u);
}

CRGB lerpColor(const CRGB& a, const CRGB& b, uint8_t t) {
  return CRGB(lerpChannel(a.r, b.r, t), lerpChannel(a.g, b.g, t), lerpChannel(a.b, b.b, t));
}

}  // namespace

Palettes::Palettes() {
  palettes_[0] = RainbowColors_p;
  palettes_[1] = PartyColors_p;
  palettes_[2] = OceanColors_p;
  palettes_[3] = ForestColors_p;
  palettes_[4] = LavaColors_p;
  palettes_[5] = HeatColors_p;
  palettes_[6] = CloudColors_p;
  setUserColors(CRGB::Red, CRGB::Green, CRGB::Blue);
}

uint8_t Palettes::clampIndex(int32_t index) {
  if (index < 0 || index >= kCount) {
    return kDefaultIndex;
  }
  return static_cast<uint8_t>(index);
}

const char* Palettes::id(int32_t index) {
  return kInfo[clampIndex(index)].id;
}

const char* Palettes::name(int32_t index) {
  return kInfo[clampIndex(index)].name;
}

int Palettes::indexOf(const char* id) {
  if (id == nullptr) {
    return -1;
  }
  for (uint8_t i = 0; i < kCount; ++i) {
    if (std::strcmp(kInfo[i].id, id) == 0) {
      return i;
    }
  }
  return -1;
}

const CRGBPalette16& Palettes::get(int32_t index) const {
  const uint8_t valid = clampIndex(index);
  return gamma_ && valid == kUserIndex ? userCorrected_ : palettes_[valid];
}

void Palettes::setGamma(bool on) {
  gamma_ = on;
}

void Palettes::setUserColors(const CRGB& first, const CRGB& second, const CRGB& third) {
  userColors_[0] = first;
  userColors_[1] = second;
  userColors_[2] = third;
  // Own interpolation instead of fill_gradient_RGB: FastLED's fixed-point gradient
  // rounds the end colors (255 becomes 254), but user colors must stay exact.
  CRGB* entries = palettes_[kUserIndex].entries;
  for (uint8_t i = 0; i < kEntries; ++i) {
    const uint16_t position = static_cast<uint16_t>(i * 510u / (kEntries - 1));  // 0..510
    if (position <= 255) {
      entries[i] = lerpColor(first, second, static_cast<uint8_t>(position));
    } else {
      entries[i] = lerpColor(second, third, static_cast<uint8_t>(position - 255));
    }
    // The gradient runs between the chosen colors as a screen shows it; each entry
    // is then corrected, like a single user color.
    userCorrected_.entries[i] = gammaCorrect(entries[i]);
  }
}

CRGB Palettes::userColor(uint8_t slot) const {
  return slot < kUserColorCount ? userColors_[slot] : CRGB(CRGB::Black);
}

uint8_t Palettes::sample(int32_t index, CRGB* out, uint8_t count) const {
  if (out == nullptr || count == 0) {
    return 0;
  }
  const CRGBPalette16& palette = palettes_[clampIndex(index)];  // never corrected
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t position =
        count == 1 ? 0 : static_cast<uint8_t>((static_cast<uint16_t>(i) * 255) / (count - 1));
    out[i] = ColorFromPalette(palette, position, 255, LINEARBLEND_NOWRAP);
  }
  return count;
}

static_assert(Palettes::kUserIndex < Palettes::kCount, "user palette index out of range");

}  // namespace ledframe

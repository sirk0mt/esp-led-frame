// Palette list: seven built-in FastLED palettes and one user palette.
//
// Effects receive the selected palette in FrameContext. An invalid index always
// falls back to the default palette (Rainbow), so a stale index from a state file or
// the API can never select missing data.
#pragma once

#include <crgb.h>
// colorutils.h needs CRGB and CHSV, so it must follow crgb.h.
#include <colorutils.h>

#include <cstdint>

namespace ledframe {

class Palettes {
 public:
  static constexpr uint8_t kCount = 8;
  static constexpr uint8_t kDefaultIndex = 0;  // rainbow
  static constexpr uint8_t kUserIndex = 7;     // custom
  static constexpr uint8_t kUserColorCount = 3;

  // Builds the built-in palettes and the user palette red, green, blue.
  Palettes();

  static constexpr uint8_t count() { return kCount; }

  // Returns index when it is valid, otherwise kDefaultIndex.
  static uint8_t clampIndex(int32_t index);

  // Stable id for the API and state files ("rainbow", ..., "custom").
  static const char* id(int32_t index);
  // Name for the UI ("Rainbow", ..., "Custom").
  static const char* name(int32_t index);
  // Index of the palette with the given id, or -1.
  static int indexOf(const char* id);

  // Palette at index for rendering; an invalid index returns the default palette.
  // With gamma on, the user palette comes with gamma correction.
  const CRGBPalette16& get(int32_t index) const;

  // The user palette is a gradient through three colors.
  void setUserColors(const CRGB& first, const CRGB& second, const CRGB& third);
  // Gamma correction of the user palette in get(). sample() stays uncorrected: it
  // shows the colors as chosen on a screen.
  void setGamma(bool on);
  // User color 0..2; an invalid slot returns black.
  CRGB userColor(uint8_t slot) const;

  // Writes count evenly spaced colors from the start to the end of the palette,
  // without wrapping back to the first entry. Used for previews in the UI.
  // Returns the number of colors written.
  uint8_t sample(int32_t index, CRGB* out, uint8_t count) const;

 private:
  CRGBPalette16 palettes_[kCount];
  CRGBPalette16 userCorrected_;  // the user palette with gamma correction
  CRGB userColors_[kUserColorCount];
  bool gamma_ = false;
};

}  // namespace ledframe

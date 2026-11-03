// Maps logical canvas coordinates to physical LED indices.
//
// Effects always draw on a logical grid of width x height pixels, row by row from
// the top-left corner. The physical strip is wired along the rows of the panel,
// starting at `origin`. With `serpentine` every other row runs in reverse.
//
// The logical image is transformed in this order:
//   1. flipX / flipY mirror the logical image.
//   2. rotation turns the image clockwise on the panel. For 90 and 270 degrees
//      the panel is height pixels wide and width pixels tall.
//   3. The panel position is converted to a strip index from `origin`.
#pragma once

#include <cstdint>

#include "core/Limits.h"

namespace ledframe {

enum class Origin : uint8_t { kTopLeft, kTopRight, kBottomLeft, kBottomRight };

enum class Rotation : uint16_t { k0 = 0, k90 = 90, k180 = 180, k270 = 270 };

struct LayoutConfig {
  // Defaults match the v1 frame: 15 x 10, rows wired left to right from the top-left.
  uint16_t width = 15;
  uint16_t height = 10;
  bool serpentine = false;
  Origin origin = Origin::kTopLeft;
  Rotation rotation = Rotation::k0;
  bool flipX = false;
  bool flipY = false;
};

class Layout {
 public:
  static constexpr uint16_t kInvalidIndex = 0xFFFF;

  // Starts with the default LayoutConfig.
  Layout();

  // Validates the configuration and rebuilds the lookup table. On failure the
  // previous configuration and table stay active.
  bool configure(const LayoutConfig& config);
  static bool isValid(const LayoutConfig& config);

  // Physical index of a logical pixel, or kInvalidIndex when out of range.
  uint16_t map(int x, int y) const;
  uint16_t map(int logicalIndex) const;

  // Lookup table with size() entries, indexed by logical index (y * width + x).
  const uint16_t* table() const { return lut_; }

  uint16_t width() const { return config_.width; }
  uint16_t height() const { return config_.height; }
  uint16_t size() const { return static_cast<uint16_t>(config_.width * config_.height); }
  const LayoutConfig& config() const { return config_; }

 private:
  static uint16_t physicalIndex(const LayoutConfig& config, uint16_t x, uint16_t y);

  LayoutConfig config_;
  uint16_t lut_[kMaxLeds];
};

}  // namespace ledframe

#include "render/Layout.h"

namespace ledframe {

Layout::Layout() {
  configure(LayoutConfig{});
}

bool Layout::isValid(const LayoutConfig& config) {
  const uint32_t pixels = static_cast<uint32_t>(config.width) * config.height;
  if (config.width == 0 || config.height == 0 || pixels > kMaxLeds) {
    return false;
  }
  switch (config.rotation) {
    case Rotation::k0:
    case Rotation::k90:
    case Rotation::k180:
    case Rotation::k270:
      break;
    default:
      return false;
  }
  switch (config.origin) {
    case Origin::kTopLeft:
    case Origin::kTopRight:
    case Origin::kBottomLeft:
    case Origin::kBottomRight:
      return true;
  }
  return false;
}

bool Layout::configure(const LayoutConfig& config) {
  if (!isValid(config)) {
    return false;
  }
  config_ = config;
  for (uint16_t y = 0; y < config_.height; ++y) {
    for (uint16_t x = 0; x < config_.width; ++x) {
      lut_[y * config_.width + x] = physicalIndex(config_, x, y);
    }
  }
  return true;
}

uint16_t Layout::map(int x, int y) const {
  if (x < 0 || y < 0 || x >= config_.width || y >= config_.height) {
    return kInvalidIndex;
  }
  return lut_[y * config_.width + x];
}

uint16_t Layout::map(int logicalIndex) const {
  if (logicalIndex < 0 || logicalIndex >= size()) {
    return kInvalidIndex;
  }
  return lut_[logicalIndex];
}

uint16_t Layout::physicalIndex(const LayoutConfig& config, uint16_t x, uint16_t y) {
  const uint16_t w = config.width;
  const uint16_t h = config.height;
  if (config.flipX) {
    x = w - 1 - x;
  }
  if (config.flipY) {
    y = h - 1 - y;
  }

  // Panel coordinates after the clockwise rotation.
  uint16_t px = x;
  uint16_t py = y;
  uint16_t panelWidth = w;
  uint16_t panelHeight = h;
  switch (config.rotation) {
    case Rotation::k0:
      break;
    case Rotation::k90:
      px = h - 1 - y;
      py = x;
      panelWidth = h;
      panelHeight = w;
      break;
    case Rotation::k180:
      px = w - 1 - x;
      py = h - 1 - y;
      break;
    case Rotation::k270:
      px = y;
      py = w - 1 - x;
      panelWidth = h;
      panelHeight = w;
      break;
  }

  // Count rows and columns from the corner where the strip starts.
  const bool fromRight =
      config.origin == Origin::kTopRight || config.origin == Origin::kBottomRight;
  const bool fromBottom =
      config.origin == Origin::kBottomLeft || config.origin == Origin::kBottomRight;
  if (fromRight) {
    px = panelWidth - 1 - px;
  }
  if (fromBottom) {
    py = panelHeight - 1 - py;
  }
  if (config.serpentine && (py % 2) == 1) {
    px = panelWidth - 1 - px;
  }
  return static_cast<uint16_t>(py * panelWidth + px);
}

}  // namespace ledframe

// Output interface of the effect engine.
//
// LedOutput implements it with FastLED. Tests use a fake sink, so the engine
// never depends on LED hardware.
#pragma once

#include <cstdint>

#include "render/Canvas.h"
#include "render/Layout.h"

namespace ledframe {

struct Frame {
  const Canvas& canvas;  // logical pixels, row by row
  const Layout& layout;  // logical index -> physical index, same size as canvas
  uint8_t brightness;    // global brightness; 0 when the power is off
};

class IFrameSink {
 public:
  virtual ~IFrameSink() = default;
  // Called only when the frame or the brightness changed.
  virtual void show(const Frame& frame) = 0;
};

}  // namespace ledframe

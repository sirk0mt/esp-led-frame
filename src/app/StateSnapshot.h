// State snapshot published by the render task. Plain data without FreeRTOS
// types, so API serialization can use it in native tests.
#pragma once

#include <cstdint>

#include "effects/Effect.h"
#include "render/Layout.h"
#include "render/OutputMapping.h"

namespace ledframe {

struct EngineStats {
  uint16_t frameFps = 0;     // frames the engine rendered per second, changed or not
  uint16_t shownFps = 0;     // frames sent to the LEDs per second
  uint32_t renderUsAvg = 0;  // tick() without show(), average over the frames of one second
  uint32_t renderUsMax = 0;
  uint32_t showUsAvg = 0;  // show() duration, average over the shown frames
};

struct StateSnapshot {
  uint32_t version = 0;  // grows with every state change
  bool on = true;
  uint8_t brightness = 0;
  uint8_t effect = 0xFF;  // registry index, 0xFF = none
  int32_t palette = 0;
  uint32_t transitionMs = 0;
  uint8_t paramCount = 0;
  int32_t params[kMaxParams] = {};  // values of the active effect
  ColorOrder colorOrder = kDefaultColorOrder;
  LayoutConfig layout;
  uint8_t fps = 0;
  bool testPattern = false;
  EngineStats stats;  // refreshed once per second, does not change the version
};

}  // namespace ledframe

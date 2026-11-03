// Commands that change the engine state.
//
// A Command is trivially copyable and holds no pointers, so it can travel through
// a FreeRTOS queue. Effects and parameters are referenced by index: callers
// resolve ids with EffectRegistry::indexOf() and findParam() before queueing.
#pragma once

#include <cstdint>
#include <type_traits>

#include "effects/Effect.h"
#include "render/Layout.h"

namespace ledframe {

enum class CommandType : uint8_t {
  SetPower,
  SetBrightness,
  SetEffect,
  SetParams,
  SetPalette,
  SetTransition,
  SetLayout,
  // Preset: parameters of one effect, then the effect (with the normal transition),
  // palette and brightness, all in one frame. Uses effect, params, palette and
  // brightness. The power state does not change.
  ApplyPreset,
  // Shows another effect (e.g. the hidden status effect in AP mode) without making
  // it the active effect, so the saved state keeps the user's effect. effect =
  // registry index, or kNoOverride to return to the user's effect. SetEffect ends
  // an override as well.
  SetOverride,
};

struct ParamValue {
  uint8_t index;  // position in the effect's ParamSpec array
  int32_t value;  // clamped by the engine
};

struct Command {
  // SetParams target meaning "the active effect".
  static constexpr uint8_t kActiveEffect = 0xFF;
  // SetOverride value that ends the override.
  static constexpr uint8_t kNoOverride = 0xFF;

  CommandType type = CommandType::SetPower;
  bool on = true;             // SetPower
  uint8_t brightness = 0;     // SetBrightness, ApplyPreset
  uint8_t effect = 0;         // SetEffect, ApplyPreset: registry index; SetParams: or kActiveEffect
  int32_t palette = 0;        // SetPalette, ApplyPreset: palette index
  uint32_t transitionMs = 0;  // SetTransition
  uint8_t paramCount = 0;     // SetParams, ApplyPreset
  ParamValue params[kMaxParams] = {};
  LayoutConfig layout;  // SetLayout

  static Command setPower(bool value) {
    Command c;
    c.type = CommandType::SetPower;
    c.on = value;
    return c;
  }

  static Command setBrightness(uint8_t value) {
    Command c;
    c.type = CommandType::SetBrightness;
    c.brightness = value;
    return c;
  }

  static Command setEffect(uint8_t registryIndex) {
    Command c;
    c.type = CommandType::SetEffect;
    c.effect = registryIndex;
    return c;
  }

  // Start with setParams(), then add values with addParam().
  static Command setParams(uint8_t registryIndex = kActiveEffect) {
    Command c;
    c.type = CommandType::SetParams;
    c.effect = registryIndex;
    return c;
  }

  static Command setPalette(int32_t index) {
    Command c;
    c.type = CommandType::SetPalette;
    c.palette = index;
    return c;
  }

  static Command setTransition(uint32_t ms) {
    Command c;
    c.type = CommandType::SetTransition;
    c.transitionMs = ms;
    return c;
  }

  static Command setLayout(const LayoutConfig& config) {
    Command c;
    c.type = CommandType::SetLayout;
    c.layout = config;
    return c;
  }

  static Command setOverride(uint8_t registryIndex) {
    Command c;
    c.type = CommandType::SetOverride;
    c.effect = registryIndex;
    return c;
  }

  // Start with applyPreset(), then add the effect's values with addParam().
  static Command applyPreset(uint8_t registryIndex, int32_t paletteIndex, uint8_t level) {
    Command c;
    c.type = CommandType::ApplyPreset;
    c.effect = registryIndex;
    c.palette = paletteIndex;
    c.brightness = level;
    return c;
  }

  // Adds a parameter value to a SetParams or ApplyPreset command. Returns false when full.
  bool addParam(uint8_t index, int32_t value) {
    if (paramCount >= kMaxParams) {
      return false;
    }
    params[paramCount++] = ParamValue{index, value};
    return true;
  }
};

static_assert(std::is_trivially_copyable<Command>::value, "Command must be queueable");

}  // namespace ledframe

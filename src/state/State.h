// Working state stored in /state.json.
//
// In memory, effects and parameters are registry indices. In JSON they are ids:
//   "params": { "<effect id>": { "<param id>": value } }
// so adding, removing or reordering effects and parameters never breaks the file.
// Unknown ids in the file are skipped, missing values keep their schema defaults and
// every value is clamped like an API value.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"
#include "effects/Effect.h"
#include "effects/EffectRegistry.h"
#include "render/Palettes.h"

namespace ledframe {

class EffectEngine;

struct State {
  static constexpr uint8_t kNoEffect = 0xFF;

  bool on = true;
  uint8_t brightness = 128;
  uint8_t effect = kNoEffect;  // registry index
  uint32_t transitionMs = 500;
  int32_t palette = Palettes::kDefaultIndex;
  uint32_t userColors[Palettes::kUserColorCount] = {0xFF0000, 0x008000, 0x0000FF};  // 0xRRGGBB
  int32_t params[EffectRegistry::kMaxEffects][kMaxParams] = {};

  // Defaults: schema defaults for every effect and the default effect ("solid", or
  // the first registered effect when it is missing).
  void reset(const EffectRegistry& registry);
};

// Id of the effect active on a fresh device.
extern const char kDefaultEffectId[];

// Writes JSON to out. Returns the number of bytes the writer took; a short count
// means a write error.
size_t serializeState(const State& state, const EffectRegistry& registry, IByteWriter& out);
// Buffer form: returns the length without the terminating zero, or 0 when out is
// too small.
size_t serializeState(const State& state, const EffectRegistry& registry, char* out,
                      size_t capacity);

// Reads JSON into out, starting from out.reset(registry). Returns false when the
// text does not parse or ends early; out then holds the defaults.
bool deserializeState(IByteReader& in, const EffectRegistry& registry, State& out);
bool deserializeState(const char* json, size_t length, const EffectRegistry& registry, State& out);

// Copies the engine state (parameters of every effect) and the user palette colors.
void captureState(const EffectEngine& engine, const EffectRegistry& registry,
                  const Palettes& palettes, State& out);

// Applies the state to the engine with commands: parameters of every effect, then
// transition, brightness, power, effect and palette. Call it before the first tick()
// so the effect starts without a transition. The user palette colors are applied
// with palettes.setUserColors().
void applyState(const State& state, const EffectRegistry& registry, Palettes& palettes,
                EffectEngine& engine);

}  // namespace ledframe

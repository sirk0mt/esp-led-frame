// Named presets stored in /presets.json.
//
// A preset holds one effect with its parameters, a palette and a brightness. Each
// preset has a stable id (1..255) for the console and the API; ids are not reused
// while the preset exists. Not thread-safe: App guards it with its mutex.
//
// JSON: {"nextId":3,"presets":[{"id":1,"name":"Red","effect":"solid","bri":128,
//        "palette":0,"params":{"color":16711680}}]}
// Like State, effects and parameters are stored by id: presets with an unknown
// effect are skipped, unknown parameters are ignored and missing ones get the
// schema default.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"
#include "effects/Command.h"
#include "effects/Effect.h"
#include "effects/EffectRegistry.h"

namespace ledframe {

struct Preset {
  static constexpr size_t kNameMax = 32;

  uint8_t id = 0;
  char name[kNameMax + 1] = "";
  uint8_t effect = 0;  // registry index
  uint8_t paramCount = 0;
  int32_t params[kMaxParams] = {};  // values in ParamSpec order
  int32_t palette = 0;
  uint8_t brightness = 128;

  // ApplyPreset command for the engine.
  Command toCommand() const;
};

enum class PresetError : uint8_t {
  kNone,
  kInvalidName,
  kInvalidEffect,
  kFull,
  kNotFound,
  kNameTaken,  // rename: another preset has this name
};

class Presets {
 public:
  static constexpr uint8_t kMaxPresets = 32;

  uint8_t count() const { return count_; }
  // Preset at a list position (in creation order), or nullptr.
  const Preset* at(uint8_t position) const;
  const Preset* find(uint8_t id) const;
  const Preset* findByName(const char* name) const;

  // Saves data under its name. An existing preset with the same name is
  // overwritten and keeps its id; otherwise a new preset gets the next free id.
  // data.id is ignored. Returns the id, or 0 with error set.
  uint8_t save(const Preset& data, PresetError* error = nullptr);
  // Replaces the content of an existing preset, keeping its id and name.
  PresetError overwrite(uint8_t id, const Preset& data);
  // Gives a preset a new name; the content and id stay. Names stay unique.
  PresetError rename(uint8_t id, const char* name);
  PresetError remove(uint8_t id);
  void clear();

  // Grows with every change; App compares it to schedule a write.
  uint32_t version() const { return version_; }

  // Names: 1..32 bytes without control characters.
  static bool isValidName(const char* name);

 private:
  uint8_t allocateId() const;
  bool isValidContent(const Preset& data) const;

  Preset presets_[kMaxPresets];
  uint8_t count_ = 0;
  uint8_t nextId_ = 1;
  uint32_t version_ = 0;

  friend size_t serializePresets(const Presets&, const EffectRegistry&, IByteWriter&);
  friend bool deserializePresets(IByteReader&, const EffectRegistry&, Presets&);
};

// Fills a preset from the active effect: its parameters, palette and brightness.
Preset makePreset(const char* name, uint8_t effect, const int32_t* params, uint8_t paramCount,
                  int32_t palette, uint8_t brightness);

// Writes JSON to out. Returns the number of bytes the writer took; a short count
// means a write error.
size_t serializePresets(const Presets& presets, const EffectRegistry& registry, IByteWriter& out);
// Buffer form: returns the length without the terminating zero, or 0 when out is
// too small.
size_t serializePresets(const Presets& presets, const EffectRegistry& registry, char* out,
                        size_t capacity);

// Replaces out with the presets from JSON. Returns false when the text does not
// parse or ends early; out is then empty.
bool deserializePresets(IByteReader& in, const EffectRegistry& registry, Presets& out);
bool deserializePresets(const char* json, size_t length, const EffectRegistry& registry,
                        Presets& out);

}  // namespace ledframe

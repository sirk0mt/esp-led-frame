#include "state/Presets.h"

#include <ArduinoJson.h>

#include <cstring>

#include "render/Palettes.h"

namespace ledframe {

namespace {

int32_t clampValue(const ParamSpec& spec, int32_t value) {
  const int32_t clamped = clampParam(spec, value);
  return spec.kind == ParamKind::Palette ? Palettes::clampIndex(clamped) : clamped;
}

void copyName(char (&out)[Preset::kNameMax + 1], const char* name) {
  const size_t length = std::strlen(name);
  std::memcpy(out, name, length + 1);
}

}  // namespace

Command Preset::toCommand() const {
  Command command = Command::applyPreset(effect, palette, brightness);
  for (uint8_t i = 0; i < paramCount && i < kMaxParams; ++i) {
    command.addParam(i, params[i]);
  }
  return command;
}

const Preset* Presets::at(uint8_t position) const {
  return position < count_ ? &presets_[position] : nullptr;
}

const Preset* Presets::find(uint8_t id) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (presets_[i].id == id) {
      return &presets_[i];
    }
  }
  return nullptr;
}

const Preset* Presets::findByName(const char* name) const {
  if (name == nullptr) {
    return nullptr;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (std::strcmp(presets_[i].name, name) == 0) {
      return &presets_[i];
    }
  }
  return nullptr;
}

bool Presets::isValidName(const char* name) {
  if (name == nullptr) {
    return false;
  }
  const size_t length = std::strlen(name);
  if (length == 0 || length > Preset::kNameMax) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if (static_cast<unsigned char>(name[i]) < 0x20 || name[i] == 0x7F) {
      return false;
    }
  }
  return true;
}

bool Presets::isValidContent(const Preset& data) const {
  return data.paramCount <= kMaxParams;
}

uint8_t Presets::allocateId() const {
  // Search from nextId_ upwards and wrap around, skipping ids still in use.
  uint8_t id = nextId_ == 0 ? 1 : nextId_;
  for (int attempt = 0; attempt < 255; ++attempt) {
    if (find(id) == nullptr) {
      return id;
    }
    id = id == 255 ? 1 : static_cast<uint8_t>(id + 1);
  }
  return 0;
}

uint8_t Presets::save(const Preset& data, PresetError* error) {
  PresetError result = PresetError::kNone;
  uint8_t id = 0;
  if (!isValidName(data.name)) {
    result = PresetError::kInvalidName;
  } else if (!isValidContent(data)) {
    result = PresetError::kInvalidEffect;
  } else if (const Preset* existing = findByName(data.name)) {
    id = existing->id;
    result = overwrite(id, data);
  } else if (count_ >= kMaxPresets) {
    result = PresetError::kFull;
  } else {
    id = allocateId();
    Preset& slot = presets_[count_++];
    slot = data;
    slot.id = id;
    nextId_ = id == 255 ? 1 : static_cast<uint8_t>(id + 1);
    ++version_;
  }
  if (error != nullptr) {
    *error = result;
  }
  return result == PresetError::kNone ? id : 0;
}

PresetError Presets::overwrite(uint8_t id, const Preset& data) {
  if (!isValidContent(data)) {
    return PresetError::kInvalidEffect;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (presets_[i].id == id) {
      Preset& slot = presets_[i];
      char name[Preset::kNameMax + 1];
      copyName(name, slot.name);
      slot = data;
      slot.id = id;
      copyName(slot.name, name);
      ++version_;
      return PresetError::kNone;
    }
  }
  return PresetError::kNotFound;
}

PresetError Presets::rename(uint8_t id, const char* name) {
  if (!isValidName(name)) {
    return PresetError::kInvalidName;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (presets_[i].id != id) {
      continue;
    }
    const Preset* other = findByName(name);
    if (other != nullptr && other->id != id) {
      return PresetError::kNameTaken;
    }
    if (std::strcmp(presets_[i].name, name) != 0) {
      copyName(presets_[i].name, name);
      ++version_;
    }
    return PresetError::kNone;
  }
  return PresetError::kNotFound;
}

PresetError Presets::remove(uint8_t id) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (presets_[i].id == id) {
      for (uint8_t j = i; j + 1 < count_; ++j) {
        presets_[j] = presets_[j + 1];
      }
      --count_;
      ++version_;
      return PresetError::kNone;
    }
  }
  return PresetError::kNotFound;
}

void Presets::clear() {
  count_ = 0;
  nextId_ = 1;
  ++version_;
}

Preset makePreset(const char* name, uint8_t effect, const int32_t* params, uint8_t paramCount,
                  int32_t palette, uint8_t brightness) {
  Preset preset;
  if (name != nullptr && std::strlen(name) <= Preset::kNameMax) {
    copyName(preset.name, name);
  }
  preset.effect = effect;
  preset.paramCount = paramCount < kMaxParams ? paramCount : kMaxParams;
  for (uint8_t i = 0; i < preset.paramCount; ++i) {
    preset.params[i] = params[i];
  }
  preset.palette = palette;
  preset.brightness = brightness;
  return preset;
}

size_t serializePresets(const Presets& presets, const EffectRegistry& registry, IByteWriter& out) {
  JsonDocument doc;
  doc["nextId"] = presets.nextId_;
  JsonArray list = doc["presets"].to<JsonArray>();
  for (uint8_t i = 0; i < presets.count_; ++i) {
    const Preset& preset = presets.presets_[i];
    const EffectDescriptor* descriptor = registry.at(preset.effect);
    if (descriptor == nullptr) {
      continue;
    }
    JsonObject item = list.add<JsonObject>();
    item["id"] = preset.id;
    item["name"] = preset.name;
    item["effect"] = descriptor->id;
    item["bri"] = preset.brightness;
    item["palette"] = preset.palette;
    JsonObject params = item["params"].to<JsonObject>();
    const uint8_t count =
        preset.paramCount < descriptor->paramCount ? preset.paramCount : descriptor->paramCount;
    for (uint8_t p = 0; p < count; ++p) {
      params[descriptor->params[p].id] = preset.params[p];
    }
  }
  return serializeJson(doc, out);
}

size_t serializePresets(const Presets& presets, const EffectRegistry& registry, char* out,
                        size_t capacity) {
  return writeToArray(out, capacity, [&](IByteWriter& writer) {
    return serializePresets(presets, registry, writer);
  });
}

bool deserializePresets(const char* json, size_t length, const EffectRegistry& registry,
                        Presets& out) {
  ArrayReader reader(json, length);
  return deserializePresets(reader, registry, out);
}

bool deserializePresets(IByteReader& in, const EffectRegistry& registry, Presets& out) {
  out.count_ = 0;
  out.nextId_ = 1;
  ++out.version_;
  JsonDocument doc;
  if (deserializeJson(doc, in) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  uint8_t highestId = 0;
  for (JsonVariantConst value : root["presets"].as<JsonArrayConst>()) {
    if (out.count_ >= Presets::kMaxPresets) {
      break;
    }
    const JsonObjectConst item = value.as<JsonObjectConst>();
    const int effect =
        item["effect"].is<const char*>() ? registry.indexOf(item["effect"].as<const char*>()) : -1;
    const uint8_t id = item["id"].is<uint8_t>() ? item["id"].as<uint8_t>() : 0;
    const char* name = item["name"].is<const char*>() ? item["name"].as<const char*>() : nullptr;
    if (effect < 0 || id == 0 || out.find(id) != nullptr || !Presets::isValidName(name) ||
        out.findByName(name) != nullptr) {
      continue;  // Unknown effect, bad or duplicate id or name: skipped.
    }
    const EffectDescriptor& descriptor = *registry.at(static_cast<uint8_t>(effect));
    Preset preset;
    preset.id = id;
    copyName(preset.name, name);
    preset.effect = static_cast<uint8_t>(effect);
    preset.paramCount = descriptor.paramCount;
    loadDefaults(descriptor, preset.params);
    for (JsonPairConst entry : item["params"].as<JsonObjectConst>()) {
      const int param = findParam(descriptor, entry.key().c_str());
      if (param >= 0 && entry.value().is<int32_t>()) {
        preset.params[param] = clampValue(descriptor.params[param], entry.value().as<int32_t>());
      }
    }
    if (item["palette"].is<int32_t>()) {
      preset.palette = Palettes::clampIndex(item["palette"].as<int32_t>());
    }
    if (item["bri"].is<uint8_t>()) {
      preset.brightness = item["bri"].as<uint8_t>();
    }
    out.presets_[out.count_++] = preset;
    if (id > highestId) {
      highestId = id;
    }
  }
  if (root["nextId"].is<uint8_t>() && root["nextId"].as<uint8_t>() != 0) {
    out.nextId_ = root["nextId"].as<uint8_t>();
  } else if (highestId > 0) {
    out.nextId_ = highestId == 255 ? 1 : static_cast<uint8_t>(highestId + 1);
  }
  return true;
}

}  // namespace ledframe

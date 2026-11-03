#include "effects/Effect.h"

#include <cstring>

namespace ledframe {

uint8_t selectOptionCount(const ParamSpec& spec) {
  if (spec.options == nullptr || spec.options[0] == '\0') {
    return 0;
  }
  uint8_t count = 1;
  for (const char* c = spec.options; *c != '\0'; ++c) {
    if (*c == '|' && count < 255) {
      ++count;
    }
  }
  return count;
}

int32_t clampParam(const ParamSpec& spec, int32_t value) {
  switch (spec.kind) {
    case ParamKind::Slider:
      if (value < spec.min) {
        return spec.min;
      }
      return value > spec.max ? spec.max : value;
    case ParamKind::Toggle:
      return value != 0 ? 1 : 0;
    case ParamKind::Color:
      return static_cast<int32_t>(static_cast<uint32_t>(value) & 0xFFFFFFu);
    case ParamKind::Select: {
      const int32_t last = static_cast<int32_t>(selectOptionCount(spec)) - 1;
      if (value < 0 || last < 0) {
        return 0;
      }
      return value > last ? last : value;
    }
    case ParamKind::Palette:
      return value < 0 ? 0 : value;
  }
  return 0;
}

void loadDefaults(const EffectDescriptor& descriptor, int32_t* values) {
  for (uint8_t i = 0; i < descriptor.paramCount; ++i) {
    values[i] = defaultParam(descriptor.params[i]);
  }
}

void clampParams(const EffectDescriptor& descriptor, int32_t* values) {
  for (uint8_t i = 0; i < descriptor.paramCount; ++i) {
    values[i] = clampParam(descriptor.params[i], values[i]);
  }
}

Effect* EffectSlot::create(const EffectDescriptor& descriptor) {
  destroy();
  effect_ = descriptor.create != nullptr ? descriptor.create(memory_) : nullptr;
  return effect_;
}

void EffectSlot::destroy() {
  if (effect_ != nullptr) {
    effect_->~Effect();
    effect_ = nullptr;
  }
}

int findParam(const EffectDescriptor& descriptor, const char* id) {
  if (id == nullptr) {
    return -1;
  }
  for (uint8_t i = 0; i < descriptor.paramCount; ++i) {
    if (std::strcmp(descriptor.params[i].id, id) == 0) {
      return i;
    }
  }
  return -1;
}

}  // namespace ledframe

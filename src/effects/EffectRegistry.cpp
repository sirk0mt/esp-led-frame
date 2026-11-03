#include "effects/EffectRegistry.h"

#include <cstring>

#include "core/Log.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "registry";

bool isValidId(const char* id) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  for (const char* c = id; *c != '\0'; ++c) {
    const bool allowed =
        (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool isValidParam(const ParamSpec& spec) {
  if (!isValidId(spec.id) || spec.label == nullptr) {
    return false;
  }
  switch (spec.kind) {
    case ParamKind::Slider:
      return spec.min <= spec.max && spec.def >= spec.min && spec.def <= spec.max;
    case ParamKind::Toggle:
      return spec.def == 0 || spec.def == 1;
    case ParamKind::Color:
      return spec.def >= 0 && spec.def <= 0xFFFFFF;
    case ParamKind::Select:
      return selectOptionCount(spec) > 0 && spec.def >= 0 && spec.def < selectOptionCount(spec);
    case ParamKind::Palette:
      return spec.def >= 0;
  }
  return false;
}

// Strict weak order: by order, then by id.
bool goesBefore(const EffectDescriptor& a, const EffectDescriptor& b) {
  if (a.order != b.order) {
    return a.order < b.order;
  }
  return std::strcmp(a.id, b.id) < 0;
}

const char* errorName(RegistryError error) {
  switch (error) {
    case RegistryError::kNone:
      return "none";
    case RegistryError::kDuplicateId:
      return "duplicate id";
    case RegistryError::kInvalidDescriptor:
      return "invalid descriptor";
    case RegistryError::kFull:
      return "registry full";
  }
  return "unknown";
}

}  // namespace

EffectRegistry& EffectRegistry::global() {
  static EffectRegistry registry;
  return registry;
}

bool EffectRegistry::isValid(const EffectDescriptor& descriptor) {
  if (!isValidId(descriptor.id) || descriptor.name == nullptr || descriptor.create == nullptr) {
    return false;
  }
  if (descriptor.paramCount > kMaxParams) {
    return false;
  }
  if (descriptor.paramCount > 0 && descriptor.params == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < descriptor.paramCount; ++i) {
    if (!isValidParam(descriptor.params[i])) {
      return false;
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (std::strcmp(descriptor.params[i].id, descriptor.params[j].id) == 0) {
        return false;
      }
    }
  }
  return true;
}

RegistryError EffectRegistry::add(const EffectDescriptor* descriptor) {
  if (descriptor == nullptr || !isValid(*descriptor)) {
    const char* id = descriptor != nullptr ? descriptor->id : nullptr;
    recordIssue(RegistryError::kInvalidDescriptor, id);
    return RegistryError::kInvalidDescriptor;
  }
  if (find(descriptor->id) != nullptr) {
    recordIssue(RegistryError::kDuplicateId, descriptor->id);
    return RegistryError::kDuplicateId;
  }
  if (count_ >= kMaxEffects) {
    recordIssue(RegistryError::kFull, descriptor->id);
    return RegistryError::kFull;
  }
  // Insertion sort keeps entries ordered by (order, id).
  uint8_t position = count_;
  while (position > 0 && goesBefore(*descriptor, *entries_[position - 1])) {
    entries_[position] = entries_[position - 1];
    --position;
  }
  entries_[position] = descriptor;
  ++count_;
  return RegistryError::kNone;
}

const EffectDescriptor* EffectRegistry::at(uint8_t index) const {
  return index < count_ ? entries_[index] : nullptr;
}

const EffectDescriptor* EffectRegistry::find(const char* id) const {
  const int index = indexOf(id);
  return index < 0 ? nullptr : entries_[index];
}

int EffectRegistry::indexOf(const char* id) const {
  if (id == nullptr) {
    return -1;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i]->id, id) == 0) {
      return i;
    }
  }
  return -1;
}

void EffectRegistry::recordIssue(RegistryError error, const char* id) {
  if (issueCount_ < kMaxIssues) {
    issues_[issueCount_] = RegistryIssue{error, id};
  }
  if (issueCount_ < 255) {
    ++issueCount_;
  }
}

void EffectRegistry::logIssues() const {
  const uint8_t stored = issueCount_ < kMaxIssues ? issueCount_ : kMaxIssues;
  for (uint8_t i = 0; i < stored; ++i) {
    LOG_E(kTag, "effect '%s' rejected: %s", issues_[i].id != nullptr ? issues_[i].id : "(null)",
          errorName(issues_[i].error));
  }
  if (issueCount_ > stored) {
    LOG_E(kTag, "%u more registration issues not stored",
          static_cast<unsigned>(issueCount_ - stored));
  }
}

}  // namespace ledframe

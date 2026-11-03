// Registry of effect descriptors.
//
// Effects register themselves during static initialization with REGISTER_EFFECT.
// The registry never allocates: it stores pointers in a fixed array, sorted by
// (order, id) so iteration does not depend on the static initialization order.
// Problems found during registration (duplicate id, invalid descriptor, full
// registry) are recorded and reported at startup with logIssues().
#pragma once

#include <cstdint>
#include <new>

#include "effects/Effect.h"

namespace ledframe {

enum class RegistryError : uint8_t { kNone, kDuplicateId, kInvalidDescriptor, kFull };

struct RegistryIssue {
  RegistryError error;
  const char* id;  // id of the rejected descriptor; may be nullptr
};

class EffectRegistry {
 public:
  static constexpr uint8_t kMaxEffects = 32;
  static constexpr uint8_t kMaxIssues = 8;

  // Registry filled by REGISTER_EFFECT. Constructed on first use, which avoids the
  // static initialization order problem.
  static EffectRegistry& global();

  // Adds a descriptor. A rejected descriptor is recorded as an issue.
  RegistryError add(const EffectDescriptor* descriptor);

  // Checks ids, names, the factory and every ParamSpec, including default values.
  static bool isValid(const EffectDescriptor& descriptor);

  uint8_t size() const { return count_; }
  // Descriptor at a position in (order, id) order, or nullptr.
  const EffectDescriptor* at(uint8_t index) const;
  const EffectDescriptor* find(const char* id) const;
  int indexOf(const char* id) const;

  const EffectDescriptor* const* begin() const { return entries_; }
  const EffectDescriptor* const* end() const { return entries_ + count_; }

  // Every rejected registration counts, but only the first kMaxIssues are stored.
  uint8_t issueCount() const { return issueCount_; }
  const RegistryIssue& issue(uint8_t index) const { return issues_[index]; }
  bool ok() const { return issueCount_ == 0; }
  // Logs every stored issue with LOG_E. Call once at startup.
  void logIssues() const;

 private:
  void recordIssue(RegistryError error, const char* id);

  const EffectDescriptor* entries_[kMaxEffects] = {};
  uint8_t count_ = 0;
  RegistryIssue issues_[kMaxIssues] = {};
  uint8_t issueCount_ = 0;
};

// Adds a descriptor to a registry from a static constructor.
class EffectRegistrar {
 public:
  EffectRegistrar(EffectRegistry& registry, const EffectDescriptor* descriptor) {
    registry.add(descriptor);
  }
};

}  // namespace ledframe

#define LEDFRAME_CONCAT_INNER(a, b) a##b
#define LEDFRAME_CONCAT(a, b) LEDFRAME_CONCAT_INNER(a, b)

// Registers an effect class. Use it at global scope in the effect's .cpp file, after
// the class definition. EffectClass must be a plain identifier (no namespace
// qualifier) and must be default-constructible. params is a ParamSpec array or nullptr.
// The class must fit in an EffectSlot (docs/ADDING_EFFECT.md).
#define REGISTER_EFFECT_WITH_WIDGET(EffectClass, id, name, order, flags, params, widget)         \
  namespace {                                                                                    \
  static_assert(sizeof(EffectClass) <= ::ledframe::kEffectSlotSize,                              \
                "effect object larger than kEffectSlotSize: keep at most 2 bytes per LED in "    \
                "the object or raise kEffectSlotSize in effects/Effect.h (ADDING_EFFECT.md)");   \
  static_assert(alignof(EffectClass) <= ::ledframe::kEffectSlotAlign,                            \
                "effect object needs a stricter alignment than kEffectSlotAlign");               \
  ::ledframe::Effect* LEDFRAME_CONCAT(ledframeCreate_, EffectClass)(void* memory) {              \
    return new (memory) EffectClass();                                                           \
  }                                                                                              \
  const ::ledframe::EffectDescriptor LEDFRAME_CONCAT(ledframeDescriptor_, EffectClass) = {       \
      id,                                                                                        \
      name,                                                                                      \
      order,                                                                                     \
      static_cast<uint8_t>(flags),                                                               \
      ::ledframe::paramsOf(params),                                                              \
      ::ledframe::paramCountOf(params),                                                          \
      widget,                                                                                    \
      &LEDFRAME_CONCAT(ledframeCreate_, EffectClass)};                                           \
  const ::ledframe::EffectRegistrar LEDFRAME_CONCAT(ledframeRegistrar_, EffectClass)(            \
      ::ledframe::EffectRegistry::global(), &LEDFRAME_CONCAT(ledframeDescriptor_, EffectClass)); \
  }  // namespace

#define REGISTER_EFFECT(EffectClass, id, name, order, flags, params) \
  REGISTER_EFFECT_WITH_WIDGET(EffectClass, id, name, order, flags, params, nullptr)

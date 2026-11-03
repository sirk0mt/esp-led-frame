#include <unity.h>

#include <cstdio>
#include <cstring>

#include "effects/EffectRegistry.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;
using ledframe::RegistryError;

// Unity compares integers; enum class values need an explicit cast.
#define ASSERT_REGISTRY_ERROR(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

// --- Effects registered in the global registry by REGISTER_EFFECT -----------

namespace {

constexpr ParamSpec kAlphaParams[] = {
    {"speed", "Speed", ParamKind::Slider, 0, 255, 64},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFF0000},
};

class AlphaEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    ctx.canvas.fill(CRGB(static_cast<uint32_t>(ctx.params[1])));
    return true;
  }
};

class BetaEffect : public Effect {
 public:
  bool render(FrameContext& /*ctx*/) override { return false; }
};

// Same id as AlphaEffect: must be rejected at startup.
class AlphaDuplicateEffect : public Effect {
 public:
  bool render(FrameContext& /*ctx*/) override { return false; }
};

}  // namespace

// Registered in reverse UI order to check sorting.
REGISTER_EFFECT(BetaEffect, "test_beta", "Beta", 20, ledframe::kFlag2D, nullptr);
REGISTER_EFFECT(AlphaEffect, "test_alpha", "Alpha", 10, ledframe::kFlagPalette, kAlphaParams);
REGISTER_EFFECT(AlphaDuplicateEffect, "test_alpha", "Alpha again", 5, 0, nullptr);

// --- Helpers for local registries -------------------------------------------

namespace {

class NullEffect : public Effect {
 public:
  bool render(FrameContext& /*ctx*/) override { return false; }
};

Effect* createNull(void* memory) {
  return new (memory) NullEffect();
}

EffectDescriptor makeDescriptor(const char* id, uint16_t order) {
  return EffectDescriptor{id, "Test", order, 0, nullptr, 0, nullptr, &createNull};
}

Canvas canvas;

}  // namespace

void setUp() {}
void tearDown() {}

void test_global_registry_contains_both_effects() {
  // Built-in effects from src/effects may be registered too, so only test_* ids count.
  const EffectRegistry& registry = EffectRegistry::global();
  TEST_ASSERT_TRUE(registry.size() >= 2);
  TEST_ASSERT_NOT_NULL(registry.find("test_alpha"));
  TEST_ASSERT_NOT_NULL(registry.find("test_beta"));
  TEST_ASSERT_NULL(registry.find("missing"));
  TEST_ASSERT_NULL(registry.find(nullptr));
}

void test_global_registry_iterates_in_order() {
  const EffectRegistry& registry = EffectRegistry::global();
  const char* expected[] = {"test_alpha", "test_beta"};
  int i = 0;
  uint16_t previousOrder = 0;
  for (const EffectDescriptor* descriptor : registry) {
    TEST_ASSERT_TRUE(descriptor->order >= previousOrder);
    previousOrder = descriptor->order;
    if (std::strncmp(descriptor->id, "test_", 5) == 0) {
      TEST_ASSERT_TRUE(i < 2);
      TEST_ASSERT_EQUAL_STRING(expected[i++], descriptor->id);
    }
  }
  TEST_ASSERT_EQUAL_INT(2, i);
  TEST_ASSERT_TRUE(registry.indexOf("test_alpha") < registry.indexOf("test_beta"));
  TEST_ASSERT_EQUAL_STRING("test_beta", registry.at(registry.indexOf("test_beta"))->id);
  TEST_ASSERT_NULL(registry.at(registry.size()));
  TEST_ASSERT_EQUAL_INT(-1, registry.indexOf("missing"));
}

void test_duplicate_id_detected_at_startup() {
  const EffectRegistry& registry = EffectRegistry::global();
  TEST_ASSERT_FALSE(registry.ok());
  TEST_ASSERT_EQUAL_UINT8(1, registry.issueCount());
  ASSERT_REGISTRY_ERROR(RegistryError::kDuplicateId, registry.issue(0).error);
  TEST_ASSERT_EQUAL_STRING("test_alpha", registry.issue(0).id);
  // The first registration stays active.
  TEST_ASSERT_EQUAL_STRING("Alpha", registry.find("test_alpha")->name);
  registry.logIssues();
}

void test_macro_fills_descriptor() {
  const EffectDescriptor* alpha = EffectRegistry::global().find("test_alpha");
  TEST_ASSERT_EQUAL_STRING("Alpha", alpha->name);
  TEST_ASSERT_EQUAL_UINT16(10, alpha->order);
  TEST_ASSERT_EQUAL_UINT8(ledframe::kFlagPalette, alpha->flags);
  TEST_ASSERT_EQUAL_UINT8(2, alpha->paramCount);
  TEST_ASSERT_EQUAL_PTR(kAlphaParams, alpha->params);
  TEST_ASSERT_NULL(alpha->widget);

  const EffectDescriptor* beta = EffectRegistry::global().find("test_beta");
  TEST_ASSERT_EQUAL_UINT8(0, beta->paramCount);
  TEST_ASSERT_NULL(beta->params);
}

void test_factory_creates_working_effect() {
  const EffectDescriptor* alpha = EffectRegistry::global().find("test_alpha");
  int32_t params[ledframe::kMaxParams];
  ledframe::loadDefaults(*alpha, params);
  TEST_ASSERT_TRUE(canvas.resize(2, 2));
  CRGBPalette16 palette;
  FrameContext ctx{canvas, 0, 0, params, palette};

  ledframe::EffectSlot slot;
  Effect* effect = slot.create(*alpha);
  TEST_ASSERT_NOT_NULL(effect);
  effect->begin(ctx);
  TEST_ASSERT_TRUE(effect->render(ctx));
  TEST_ASSERT_EQUAL_UINT8(255, canvas[3].r);
  TEST_ASSERT_EQUAL_UINT8(0, canvas[3].g);
}

void test_local_registry_rejects_invalid_descriptors() {
  static EffectRegistry registry;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(nullptr));

  EffectDescriptor badId = makeDescriptor("Bad Id", 1);
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&badId));
  EffectDescriptor emptyId = makeDescriptor("", 1);
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&emptyId));
  EffectDescriptor noFactory = makeDescriptor("no_factory", 1);
  noFactory.create = nullptr;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&noFactory));
  EffectDescriptor missingParams = makeDescriptor("missing_params", 1);
  missingParams.paramCount = 2;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&missingParams));

  static constexpr ParamSpec kBadDefault[] = {{"speed", "Speed", ParamKind::Slider, 0, 10, 11}};
  EffectDescriptor badDefault = makeDescriptor("bad_default", 1);
  badDefault.params = kBadDefault;
  badDefault.paramCount = 1;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&badDefault));

  static constexpr ParamSpec kDuplicateParam[] = {
      {"speed", "Speed", ParamKind::Slider, 0, 10, 1},
      {"speed", "Speed 2", ParamKind::Slider, 0, 10, 1},
  };
  EffectDescriptor duplicateParam = makeDescriptor("duplicate_param", 1);
  duplicateParam.params = kDuplicateParam;
  duplicateParam.paramCount = 2;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&duplicateParam));

  static constexpr ParamSpec kEmptySelect[] = {{"mode", "Mode", ParamKind::Select, 0, 0, 0, ""}};
  EffectDescriptor emptySelect = makeDescriptor("empty_select", 1);
  emptySelect.params = kEmptySelect;
  emptySelect.paramCount = 1;
  ASSERT_REGISTRY_ERROR(RegistryError::kInvalidDescriptor, registry.add(&emptySelect));

  TEST_ASSERT_EQUAL_UINT8(0, registry.size());
  TEST_ASSERT_EQUAL_UINT8(8, registry.issueCount());
}

void test_local_registry_orders_ties_by_id() {
  static EffectRegistry registry;
  static EffectDescriptor c = makeDescriptor("c", 5);
  static EffectDescriptor a = makeDescriptor("a", 5);
  static EffectDescriptor b = makeDescriptor("b", 1);
  ASSERT_REGISTRY_ERROR(RegistryError::kNone, registry.add(&c));
  ASSERT_REGISTRY_ERROR(RegistryError::kNone, registry.add(&a));
  ASSERT_REGISTRY_ERROR(RegistryError::kNone, registry.add(&b));
  TEST_ASSERT_EQUAL_STRING("b", registry.at(0)->id);
  TEST_ASSERT_EQUAL_STRING("a", registry.at(1)->id);
  TEST_ASSERT_EQUAL_STRING("c", registry.at(2)->id);
  TEST_ASSERT_TRUE(registry.ok());
}

void test_local_registry_full() {
  static EffectRegistry registry;
  static char ids[EffectRegistry::kMaxEffects + 1][8];
  static EffectDescriptor descriptors[EffectRegistry::kMaxEffects + 1];
  for (int i = 0; i <= EffectRegistry::kMaxEffects; ++i) {
    std::snprintf(ids[i], sizeof(ids[i]), "e%d", i);
    descriptors[i] = makeDescriptor(ids[i], static_cast<uint16_t>(i));
  }
  for (int i = 0; i < EffectRegistry::kMaxEffects; ++i) {
    ASSERT_REGISTRY_ERROR(RegistryError::kNone, registry.add(&descriptors[i]));
  }
  ASSERT_REGISTRY_ERROR(RegistryError::kFull,
                        registry.add(&descriptors[EffectRegistry::kMaxEffects]));
  TEST_ASSERT_EQUAL_UINT8(EffectRegistry::kMaxEffects, registry.size());
  ASSERT_REGISTRY_ERROR(RegistryError::kFull, registry.issue(0).error);
}

void test_issue_storage_is_bounded() {
  static EffectRegistry registry;
  for (int i = 0; i < EffectRegistry::kMaxIssues + 3; ++i) {
    registry.add(nullptr);
  }
  TEST_ASSERT_EQUAL_UINT8(EffectRegistry::kMaxIssues + 3, registry.issueCount());
  registry.logIssues();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_global_registry_contains_both_effects);
  RUN_TEST(test_global_registry_iterates_in_order);
  RUN_TEST(test_duplicate_id_detected_at_startup);
  RUN_TEST(test_macro_fills_descriptor);
  RUN_TEST(test_factory_creates_working_effect);
  RUN_TEST(test_local_registry_rejects_invalid_descriptors);
  RUN_TEST(test_local_registry_orders_ties_by_id);
  RUN_TEST(test_local_registry_full);
  RUN_TEST(test_issue_storage_is_bounded);
  return UNITY_END();
}

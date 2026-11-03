// Effect contract.
//
// An effect is a class derived from Effect plus a static EffectDescriptor that
// describes it. REGISTER_EFFECT (EffectRegistry.h) creates the descriptor and adds
// it to the global registry, so a new effect needs only its own .cpp file.
#pragma once

#include <crgb.h>
// colorutils.h needs CRGB and CHSV, so it must follow crgb.h.
#include <colorutils.h>

#include <cstddef>
#include <cstdint>

#include "core/Limits.h"
#include "core/WallTime.h"
#include "render/Canvas.h"
#include "render/Gamma.h"

namespace ledframe {

enum class ParamKind : uint8_t { Slider, Toggle, Color, Select, Palette };

struct ParamSpec {
  const char* id;     // "speed": stable key in the API and in the state file
  const char* label;  // "Speed": label in the UI
  ParamKind kind;
  int32_t min, max, def;          // Color: 0xRRGGBB; Toggle: 0/1; Palette: palette index
  const char* options = nullptr;  // Select: "random|palette"
};

enum EffectFlags : uint8_t {
  kFlag2D = 1 << 0,       // uses x,y (the UI may warn on a 1D strip)
  kFlagPalette = 1 << 1,  // the UI shows the palette selector
  kFlagStatic = 1 << 2,   // draws only after parameter changes
  kFlagHidden = 1 << 3,   // not shown in the UI (e.g. status)
};

// Maximum number of parameters per effect.
inline constexpr uint8_t kMaxParams = 16;

struct FrameContext {
  Canvas& canvas;
  uint32_t nowMs;         // time since start
  uint32_t deltaMs;       // time since the previous frame
  const int32_t* params;  // values in ParamSpec order, always valid (see clampParam)
  const CRGBPalette16& palette;
  bool gamma = false;  // Config::gamma: user colors get gamma correction
  // Local wall-clock time, refreshed at most once per second. known is false
  // until SNTP synchronized the clock, and always in tests unless set by hand.
  WallTime wallTime;

  // A color the user chose on a screen (a Color parameter 0xRRGGBB or a painted
  // pixel) as the LEDs should show it. Effects draw user colors through these, never
  // with their own correction; palette colors need neither.
  CRGB userColor(int32_t rgb) const { return userColor(CRGB(static_cast<uint32_t>(rgb))); }
  CRGB userColor(const CRGB& color) const { return gamma ? gammaCorrect(color) : color; }
};

class Effect {
 public:
  virtual ~Effect() = default;
  // Called once after creation. Prepare size-dependent data here. Buffers are
  // fixed-size members that fit in an EffectSlot; effects never allocate.
  virtual void begin(FrameContext& /*ctx*/) {}
  // Draws one frame. Returns false when the frame did not change.
  virtual bool render(FrameContext& ctx) = 0;
  virtual void onParamChanged(uint8_t /*index*/, FrameContext& /*ctx*/) {}
};

struct EffectDescriptor {
  const char* id;    // "rainbow": lowercase letters, digits, '_' and '-'
  const char* name;  // "Rainbow"
  uint16_t order;    // position in the UI, lower first
  uint8_t flags;     // EffectFlags
  const ParamSpec* params;
  uint8_t paramCount;
  const char* widget;  // nullptr or e.g. "paint"
  // Constructs the effect in memory of an EffectSlot (placement new). Returns
  // nullptr when it cannot.
  Effect* (*create)(void* memory);
};

// --- Effect memory ------------------------------------------------------------

// Size of one effect object. 2 bytes per LED cover the largest built-in effect
// (radial-rainbow keeps a distance per LED); 64 bytes are left for other members.
// REGISTER_EFFECT refuses a larger class at compile time.
inline constexpr size_t kEffectSlotSize = 2 * static_cast<size_t>(kMaxLeds) + 64;
inline constexpr size_t kEffectSlotAlign = alignof(std::max_align_t);

// Memory for one effect, so creating an effect never allocates and cannot run out
// of memory. The engine has two (the effect shown and the old one during a
// transition); a test uses one to run a single effect.
class EffectSlot {
 public:
  EffectSlot() = default;
  ~EffectSlot() { destroy(); }
  EffectSlot(const EffectSlot&) = delete;
  EffectSlot& operator=(const EffectSlot&) = delete;

  // Destroys the effect in the slot, then constructs one with the descriptor's
  // factory. Returns it, or nullptr when the factory fails.
  Effect* create(const EffectDescriptor& descriptor);
  // Runs the destructor of the effect in the slot, if any.
  void destroy();
  Effect* get() const { return effect_; }

 private:
  alignas(kEffectSlotAlign) unsigned char memory_[kEffectSlotSize];
  Effect* effect_ = nullptr;
};

// --- Parameter values -------------------------------------------------------

// Number of options in a Select parameter ("a|b|c" has 3). Zero when options is empty.
uint8_t selectOptionCount(const ParamSpec& spec);

// Returns a valid value for the parameter:
//   Slider  - clamped to [min, max]
//   Toggle  - 1 for any non-zero value, otherwise 0
//   Color   - masked to 24 bits (0xRRGGBB)
//   Select  - clamped to [0, option count - 1]
//   Palette - at least 0; the upper bound depends on the palette list
int32_t clampParam(const ParamSpec& spec, int32_t value);

// The parameter default, passed through clampParam.
inline int32_t defaultParam(const ParamSpec& spec) {
  return clampParam(spec, spec.def);
}

// Writes descriptor.paramCount default values to values.
void loadDefaults(const EffectDescriptor& descriptor, int32_t* values);

// Clamps descriptor.paramCount values in place.
void clampParams(const EffectDescriptor& descriptor, int32_t* values);

// Index of the parameter with the given id, or -1.
int findParam(const EffectDescriptor& descriptor, const char* id);

// --- Registration helpers used by REGISTER_EFFECT ---------------------------

template <size_t N>
constexpr const ParamSpec* paramsOf(const ParamSpec (&params)[N]) {
  return params;
}
constexpr const ParamSpec* paramsOf(std::nullptr_t) {
  return nullptr;
}

template <size_t N>
constexpr uint8_t paramCountOf(const ParamSpec (&)[N]) {
  static_assert(N <= kMaxParams, "too many effect parameters");
  return static_cast<uint8_t>(N);
}
constexpr uint8_t paramCountOf(std::nullptr_t) {
  return 0;
}

}  // namespace ledframe

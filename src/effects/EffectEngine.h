// Single-threaded effect engine.
//
// The engine owns the active effect, the per-effect parameter values, the layout
// and the frame buffers. It changes state only through apply(Command) and draws
// only in tick(nowMs), so the caller decides the threading model and tests
// can drive it with a fake clock and a fake IFrameSink.
//
// Buffers: two effect canvases (old and new effect during a transition) and one
// canvas for the blended output. Each effect keeps its own canvas between frames,
// so effects may build on the previous frame (e.g. fadeToBlackBy). Effect objects
// live in two EffectSlots with the same indices as the effect canvases, so
// the engine never allocates.
#pragma once

#include <cstdint>

#include "effects/Command.h"
#include "effects/Effect.h"
#include "effects/EffectRegistry.h"
#include "render/Canvas.h"
#include "render/IFrameSink.h"
#include "render/Layout.h"
#include "render/Palettes.h"

namespace ledframe {

class EffectEngine {
 public:
  static constexpr uint32_t kDefaultTransitionMs = 500;
  static constexpr uint32_t kMaxTransitionMs = 10000;
  static constexpr uint8_t kDefaultFps = 50;
  static constexpr uint8_t kMinFps = 10;
  static constexpr uint8_t kMaxFps = 120;
  static constexpr uint32_t kEarlyMs = 1;
  static constexpr uint8_t kDefaultBrightness = 128;
  static constexpr uint8_t kNoEffect = 0xFF;

  // The registry must be complete (static initialization finished) and must not
  // change afterwards: parameter storage is indexed by registry position.
  EffectEngine(const EffectRegistry& registry, Palettes& palettes, IFrameSink& sink);
  ~EffectEngine();
  EffectEngine(const EffectEngine&) = delete;
  EffectEngine& operator=(const EffectEngine&) = delete;

  // Applies a command. Returns true when the state changed. Invalid targets are
  // ignored and out-of-range values are clamped.
  bool apply(const Command& command);

  // Renders a frame when one is due and sends it to the sink when it changed.
  // Returns true when the sink received a frame. The engine alone keeps the frame
  // schedule: frames are 1000000 / fps µs apart on average, and a frame is
  // taken up to kEarlyMs before its time, so a caller that wakes a little early
  // (another clock, timer rounding) never loses it.
  bool tick(uint32_t nowMs);

  // When the next frame is due (nowMs scale). Before the first frame it is 0, so
  // the first tick() renders at once.
  uint32_t nextFrameMs() const { return nextFrameMs_; }
  // Frames rendered since the start, also those the sink did not receive because
  // nothing changed. For the FPS statistics.
  uint32_t frames() const { return frames_; }

  // Draws the shown effect again in the next frame, also a kFlagStatic one. For
  // effects whose data changes outside their parameters (the paint image).
  void redraw() { currentDirty_ = true; }

  // Frames per second, clamped to [kMinFps, kMaxFps].
  void setFps(uint8_t fps);
  uint8_t fps() const { return fps_; }

  // Gamma correction of user colors (Config::gamma). Shown from the next frame,
  // also for a kFlagStatic effect.
  void setGamma(bool on);
  bool gamma() const { return gamma_; }

  // Local wall-clock time for effects, set by App at most once per second.
  // Effects see it from the next frame; a kFlagStatic effect is not drawn again.
  void setWallTime(const WallTime& time) { wallTime_ = time; }
  const WallTime& wallTime() const { return wallTime_; }

  bool on() const { return on_; }
  uint8_t brightness() const { return brightness_; }
  uint32_t transitionMs() const { return transitionMs_; }
  bool inTransition() const { return previous_.effect != nullptr; }
  // Registry index of the active effect, or kNoEffect.
  uint8_t activeEffect() const;
  // Effect shown instead of the active one (SetOverride), or kNoEffect.
  uint8_t overrideEffect() const { return override_; }
  const EffectDescriptor* activeDescriptor() const;
  // Palette used by the active effect (see setPalette in EffectEngine.cpp).
  int32_t paletteIndex() const;
  // Stored values of an effect in ParamSpec order, or nullptr for a bad index.
  const int32_t* params(uint8_t effectIndex) const;
  const Layout& layout() const { return layout_; }
  // The canvas the sink receives.
  const Canvas& output() const;

 private:
  struct Slot {
    Effect* effect = nullptr;   // lives in memory_[canvas]
    uint8_t index = kNoEffect;  // registry index
    uint8_t canvas = 0;         // index into canvases_ and memory_
  };

  static constexpr uint8_t kBlendCanvas = 2;

  bool setPower(bool on);
  bool setBrightness(uint8_t brightness);
  bool setEffect(uint8_t index);
  bool setOverride(uint8_t index);
  bool switchEffect(uint8_t index);
  bool setParams(const Command& command);
  bool setPalette(int32_t index);
  bool setTransition(uint32_t ms);
  bool setLayout(const LayoutConfig& config);
  bool applyPreset(const Command& command);

  // Writes one clamped parameter value and notifies a running instance.
  bool storeParam(uint8_t effectIndex, uint8_t paramIndex, int32_t value);
  int32_t clampValue(const ParamSpec& spec, int32_t value) const;
  int paletteParam(uint8_t effectIndex) const;
  int32_t effectivePalette(uint8_t effectIndex) const;

  FrameContext context(uint8_t effectIndex, uint8_t canvasIndex, uint32_t nowMs, uint32_t deltaMs);
  Effect* create(uint8_t effectIndex, uint8_t canvasIndex);
  void destroy(Slot& slot);
  bool render(Slot& slot, uint32_t nowMs, uint32_t deltaMs, bool force);
  bool renderFrame(uint32_t nowMs, uint32_t deltaMs);
  void blend(uint8_t amount);

  const EffectRegistry& registry_;
  Palettes& palettes_;
  IFrameSink& sink_;

  Layout layout_;
  Canvas canvases_[3];
  EffectSlot memory_[2];
  Slot current_;
  Slot previous_;
  int32_t params_[EffectRegistry::kMaxEffects][kMaxParams] = {};

  bool on_ = true;
  uint8_t brightness_ = kDefaultBrightness;
  uint32_t transitionMs_ = kDefaultTransitionMs;
  int32_t palette_ = Palettes::kDefaultIndex;  // used by effects without a Palette param
  bool gamma_ = false;
  WallTime wallTime_;

  uint8_t fps_ = kDefaultFps;
  uint32_t frameIntervalUs_ = 1000000 / kDefaultFps;
  bool hasFrame_ = false;
  uint32_t lastFrameMs_ = 0;
  uint32_t nextFrameMs_ = 0;      // whole milliseconds of the next frame time
  uint32_t nextFrameFracUs_ = 0;  // and the microseconds past them, 0..999
  uint32_t frames_ = 0;

  bool currentDirty_ = true;  // forces a render of a kFlagStatic effect
  bool outputDirty_ = true;   // forces a show even when no effect changed
  bool transitionStarted_ = false;
  uint32_t transitionStartMs_ = 0;

  uint8_t override_ = kNoEffect;    // effect shown instead of the user's one
  uint8_t userEffect_ = kNoEffect;  // the user's effect while an override is active
};

}  // namespace ledframe

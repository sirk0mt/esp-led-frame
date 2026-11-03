#include "effects/EffectEngine.h"

#include "core/Log.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "engine";

}  // namespace

EffectEngine::EffectEngine(const EffectRegistry& registry, Palettes& palettes, IFrameSink& sink)
    : registry_(registry), palettes_(palettes), sink_(sink) {
  for (Canvas& canvas : canvases_) {
    canvas.resize(layout_.width(), layout_.height());
  }
  current_.canvas = 0;
  previous_.canvas = 1;
  for (uint8_t i = 0; i < registry_.size(); ++i) {
    loadDefaults(*registry_.at(i), params_[i]);
  }
}

EffectEngine::~EffectEngine() {
  destroy(previous_);
  destroy(current_);
}

bool EffectEngine::apply(const Command& command) {
  switch (command.type) {
    case CommandType::SetPower:
      return setPower(command.on);
    case CommandType::SetBrightness:
      return setBrightness(command.brightness);
    case CommandType::SetEffect:
      return setEffect(command.effect);
    case CommandType::SetParams:
      return setParams(command);
    case CommandType::SetPalette:
      return setPalette(command.palette);
    case CommandType::SetTransition:
      return setTransition(command.transitionMs);
    case CommandType::SetLayout:
      return setLayout(command.layout);
    case CommandType::ApplyPreset:
      return applyPreset(command);
    case CommandType::SetOverride:
      return setOverride(command.effect);
  }
  return false;
}

bool EffectEngine::tick(uint32_t nowMs) {
  // Signed: an early frame (within kEarlyMs) is late by -1 ms.
  const int32_t lateMs = static_cast<int32_t>(nowMs - nextFrameMs_);
  if (hasFrame_ && lateMs < -static_cast<int32_t>(kEarlyMs)) {
    return false;
  }
  const uint32_t deltaMs = hasFrame_ ? nowMs - lastFrameMs_ : 0;
  // Keep a steady cadence from the due time, not from now; after a stall of a
  // whole frame or more, start counting again from now instead of catching up.
  if (!hasFrame_ || lateMs >= static_cast<int32_t>(frameIntervalUs_ / 1000)) {
    nextFrameMs_ = nowMs;
    nextFrameFracUs_ = 0;
  }
  nextFrameFracUs_ += frameIntervalUs_;
  nextFrameMs_ += nextFrameFracUs_ / 1000;
  nextFrameFracUs_ %= 1000;
  hasFrame_ = true;
  lastFrameMs_ = nowMs;
  ++frames_;

  const bool changed = on_ && renderFrame(nowMs, deltaMs);
  if (!changed && !outputDirty_) {
    return false;
  }
  outputDirty_ = false;
  sink_.show(Frame{output(), layout_, on_ ? brightness_ : static_cast<uint8_t>(0)});
  return true;
}

void EffectEngine::setFps(uint8_t fps) {
  if (fps < kMinFps) {
    fps = kMinFps;
  } else if (fps > kMaxFps) {
    fps = kMaxFps;
  }
  fps_ = fps;
  frameIntervalUs_ = 1000000u / fps;
}

void EffectEngine::setGamma(bool on) {
  if (on == gamma_) {
    return;
  }
  gamma_ = on;
  palettes_.setGamma(on);
  currentDirty_ = true;  // A static effect draws its colors again.
  outputDirty_ = true;
}

uint8_t EffectEngine::activeEffect() const {
  return override_ != kNoEffect ? userEffect_ : current_.index;
}

const EffectDescriptor* EffectEngine::activeDescriptor() const {
  return registry_.at(activeEffect());
}

int32_t EffectEngine::paletteIndex() const {
  const uint8_t active = activeEffect();
  return active < registry_.size() ? effectivePalette(active) : palette_;
}

const int32_t* EffectEngine::params(uint8_t effectIndex) const {
  return effectIndex < registry_.size() ? params_[effectIndex] : nullptr;
}

const Canvas& EffectEngine::output() const {
  return previous_.effect != nullptr ? canvases_[kBlendCanvas] : canvases_[current_.canvas];
}

bool EffectEngine::setPower(bool on) {
  if (on == on_) {
    return false;
  }
  on_ = on;
  outputDirty_ = true;
  if (on_) {
    currentDirty_ = true;
  } else {
    destroy(previous_);  // A transition does not survive power off.
  }
  return true;
}

bool EffectEngine::setBrightness(uint8_t brightness) {
  if (brightness == brightness_) {
    return false;
  }
  brightness_ = brightness;
  outputDirty_ = true;
  return true;
}

// The new effect always gets the effect canvas the active effect does not use.
// With a transition the old effect keeps running on its canvas until the blend
// ends; without one it is destroyed right away.
bool EffectEngine::setEffect(uint8_t index) {
  if (index >= registry_.size()) {
    LOG_W(kTag, "unknown effect index %u", static_cast<unsigned>(index));
    return false;
  }
  // A choice of the user ends the override: the status effect gives way.
  const bool overrideCleared = override_ != kNoEffect;
  override_ = kNoEffect;
  userEffect_ = kNoEffect;
  return switchEffect(index) || overrideCleared;
}

// While an override is active the engine shows the override effect but reports the
// user's effect as active, so the saved state never contains the override.
bool EffectEngine::setOverride(uint8_t index) {
  if (index == override_) {
    return false;
  }
  if (index != kNoEffect && index >= registry_.size()) {
    LOG_W(kTag, "unknown override effect index %u", static_cast<unsigned>(index));
    return false;
  }
  if (index == kNoEffect) {
    const uint8_t user = userEffect_;
    override_ = kNoEffect;
    userEffect_ = kNoEffect;
    if (user < registry_.size()) {
      switchEffect(user);
    } else {
      destroy(previous_);  // No user effect before the override: show nothing.
      destroy(current_);
      canvases_[current_.canvas].clear();
      outputDirty_ = true;
    }
    return true;
  }
  if (override_ == kNoEffect) {
    userEffect_ = current_.index;
  }
  override_ = index;
  switchEffect(index);
  return true;
}

bool EffectEngine::switchEffect(uint8_t index) {
  if (index == current_.index && current_.effect != nullptr) {
    return false;
  }
  destroy(previous_);  // Interrupt a running transition.
  const uint8_t freeCanvas = current_.canvas == 0 ? 1 : 0;
  Effect* effect = create(index, freeCanvas);
  if (effect == nullptr) {
    LOG_E(kTag, "cannot create effect '%s'", registry_.at(index)->id);
    return false;
  }

  const bool animate = on_ && transitionMs_ > 0 && current_.effect != nullptr;
  if (animate) {
    previous_ = current_;
    transitionStarted_ = false;
  } else {
    destroy(current_);
  }
  current_.effect = effect;
  current_.index = index;
  current_.canvas = freeCanvas;
  currentDirty_ = true;
  outputDirty_ = true;
  return true;
}

bool EffectEngine::setParams(const Command& command) {
  const uint8_t target = command.effect == Command::kActiveEffect ? activeEffect() : command.effect;
  if (target >= registry_.size()) {
    return false;
  }
  const uint8_t count = command.paramCount < kMaxParams ? command.paramCount : kMaxParams;
  bool changed = false;
  for (uint8_t i = 0; i < count; ++i) {
    changed |= storeParam(target, command.params[i].index, command.params[i].value);
  }
  return changed;
}

// SetPalette updates the global palette and, when the active effect has a Palette
// parameter, that parameter too. The palette shown in the state is therefore always
// the one the active effect uses.
bool EffectEngine::setPalette(int32_t index) {
  const int32_t clamped = Palettes::clampIndex(index);
  bool changed = clamped != palette_;
  palette_ = clamped;
  const uint8_t active = activeEffect();
  if (active < registry_.size()) {
    const int param = paletteParam(active);
    if (param >= 0) {
      changed |= storeParam(active, static_cast<uint8_t>(param), clamped);
    } else if (changed) {
      currentDirty_ = true;
    }
  }
  return changed;
}

bool EffectEngine::setTransition(uint32_t ms) {
  const uint32_t clamped = ms > kMaxTransitionMs ? kMaxTransitionMs : ms;
  if (clamped == transitionMs_) {
    return false;
  }
  transitionMs_ = clamped;
  return true;
}

// A new layout changes the canvas size, so the active effect is created again and
// its begin() can prepare its data for the new size.
bool EffectEngine::setLayout(const LayoutConfig& config) {
  if (!layout_.configure(config)) {
    LOG_W(kTag, "invalid layout %ux%u", static_cast<unsigned>(config.width),
          static_cast<unsigned>(config.height));
    return false;
  }
  for (Canvas& canvas : canvases_) {
    canvas.resize(layout_.width(), layout_.height());
  }
  destroy(previous_);
  const uint8_t index = current_.index;
  if (current_.effect != nullptr) {
    destroy(current_);
    current_.effect = create(index, current_.canvas);
    // The user's effect stays active even when it cannot be created again, so the
    // saved state keeps it and choosing it again retries.
    current_.index = index;
    if (current_.effect == nullptr) {
      LOG_E(kTag, "cannot recreate effect '%s'", registry_.at(index)->id);
    }
  }
  currentDirty_ = true;
  outputDirty_ = true;
  return true;
}

// Parameters are stored first, so a newly created instance starts with them. The
// effect switch uses the normal transition. The palette goes last because it may
// write the new effect's Palette parameter.
bool EffectEngine::applyPreset(const Command& command) {
  if (command.effect >= registry_.size()) {
    LOG_W(kTag, "preset with unknown effect index %u", static_cast<unsigned>(command.effect));
    return false;
  }
  bool changed = false;
  const uint8_t count = command.paramCount < kMaxParams ? command.paramCount : kMaxParams;
  for (uint8_t i = 0; i < count; ++i) {
    changed |= storeParam(command.effect, command.params[i].index, command.params[i].value);
  }
  changed |= setEffect(command.effect);
  changed |= setPalette(command.palette);
  changed |= setBrightness(command.brightness);
  return changed;
}

bool EffectEngine::storeParam(uint8_t effectIndex, uint8_t paramIndex, int32_t value) {
  const EffectDescriptor& descriptor = *registry_.at(effectIndex);
  if (paramIndex >= descriptor.paramCount) {
    return false;
  }
  const int32_t clamped = clampValue(descriptor.params[paramIndex], value);
  if (params_[effectIndex][paramIndex] == clamped) {
    return false;
  }
  params_[effectIndex][paramIndex] = clamped;

  Slot* slots[] = {&current_, &previous_};
  for (Slot* slot : slots) {
    if (slot->effect != nullptr && slot->index == effectIndex) {
      FrameContext ctx = context(effectIndex, slot->canvas, lastFrameMs_, 0);
      slot->effect->onParamChanged(paramIndex, ctx);
    }
  }
  if (effectIndex == current_.index) {
    currentDirty_ = true;
  }
  return true;
}

int32_t EffectEngine::clampValue(const ParamSpec& spec, int32_t value) const {
  const int32_t clamped = clampParam(spec, value);
  return spec.kind == ParamKind::Palette ? Palettes::clampIndex(clamped) : clamped;
}

int EffectEngine::paletteParam(uint8_t effectIndex) const {
  const EffectDescriptor& descriptor = *registry_.at(effectIndex);
  for (uint8_t i = 0; i < descriptor.paramCount; ++i) {
    if (descriptor.params[i].kind == ParamKind::Palette) {
      return i;
    }
  }
  return -1;
}

int32_t EffectEngine::effectivePalette(uint8_t effectIndex) const {
  const int param = paletteParam(effectIndex);
  return param >= 0 ? params_[effectIndex][param] : palette_;
}

// What an effect sees when it draws on canvases_[canvasIndex].
FrameContext EffectEngine::context(uint8_t effectIndex, uint8_t canvasIndex, uint32_t nowMs,
                                   uint32_t deltaMs) {
  return FrameContext{canvases_[canvasIndex],
                      nowMs,
                      deltaMs,
                      params_[effectIndex],
                      palettes_.get(effectivePalette(effectIndex)),
                      gamma_,
                      wallTime_};
}

Effect* EffectEngine::create(uint8_t effectIndex, uint8_t canvasIndex) {
  const EffectDescriptor& descriptor = *registry_.at(effectIndex);
  Canvas& canvas = canvases_[canvasIndex];
  canvas.clear();
  Effect* effect = memory_[canvasIndex].create(descriptor);
  if (effect == nullptr) {
    return nullptr;
  }
  FrameContext ctx = context(effectIndex, canvasIndex, lastFrameMs_, 0);
  effect->begin(ctx);
  return effect;
}

// Only a slot with an effect owns its memory: an empty slot may name the canvas of
// the effect shown.
void EffectEngine::destroy(Slot& slot) {
  if (slot.effect != nullptr) {
    memory_[slot.canvas].destroy();
  }
  slot.effect = nullptr;
  slot.index = kNoEffect;
}

bool EffectEngine::render(Slot& slot, uint32_t nowMs, uint32_t deltaMs, bool force) {
  if (slot.effect == nullptr) {
    return false;
  }
  const EffectDescriptor& descriptor = *registry_.at(slot.index);
  if ((descriptor.flags & kFlagStatic) != 0 && !force) {
    return false;
  }
  FrameContext ctx = context(slot.index, slot.canvas, nowMs, deltaMs);
  return slot.effect->render(ctx);
}

bool EffectEngine::renderFrame(uint32_t nowMs, uint32_t deltaMs) {
  const bool changed = render(current_, nowMs, deltaMs, currentDirty_);
  currentDirty_ = false;
  if (previous_.effect == nullptr) {
    return changed;
  }

  if (!transitionStarted_) {
    transitionStarted_ = true;
    transitionStartMs_ = nowMs;
  }
  const uint32_t elapsed = nowMs - transitionStartMs_;
  if (elapsed >= transitionMs_) {
    destroy(previous_);  // Output switches back to the current canvas.
    return true;
  }
  render(previous_, nowMs, deltaMs, false);
  blend(static_cast<uint8_t>((elapsed * 255u) / transitionMs_));
  return true;
}

void EffectEngine::blend(uint8_t amount) {
  const Canvas& from = canvases_[previous_.canvas];
  const Canvas& to = canvases_[current_.canvas];
  Canvas& out = canvases_[kBlendCanvas];
  const uint16_t count = out.size();
  for (uint16_t i = 0; i < count; ++i) {
    out[i] = from[i];
    nblend(out[i], to[i], amount);
  }
}

}  // namespace ledframe

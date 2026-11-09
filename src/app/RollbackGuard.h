// When a new firmware is kept after an update, and when it is undone (rollback).
//
// After an update the bootloader starts the new firmware once as "pending verify".
// If the device resets before the firmware confirms itself, the bootloader starts the
// previous firmware again. App keeps the new one once it has run kKeepAfterMs and the
// render task still makes progress, so a firmware that crashes or restarts in a loop
// during its first seconds is undone by the reset. A render task that stops without a
// reset (nothing watches it) would leave the firmware on trial for good, so after
// kGiveUpMs without being kept App undoes the update itself.
//
// Pure logic, tested natively (test_rollback). App feeds it the time since start and a
// counter that the render task increases in every loop.
#pragma once

#include <cstdint>

namespace ledframe {

class RollbackGuard {
 public:
  static constexpr uint32_t kKeepAfterMs = 30000;
  static constexpr uint32_t kGiveUpMs = 120000;

  enum class Action : uint8_t { kWait, kKeep, kRollBack };

  // Call about once per second, always from the same task. Returns kKeep at the first
  // call at or after kKeepAfterMs where renderBeats differs from the previous call's
  // value; kRollBack at or after kGiveUpMs when the firmware was not kept by then.
  // Each of them once; afterwards kWait. The first call only records renderBeats.
  Action update(uint32_t nowMs, uint32_t renderBeats) {
    if (done_) {
      return Action::kWait;
    }
    const bool progress = started_ && renderBeats != lastBeats_;
    started_ = true;
    lastBeats_ = renderBeats;
    if (nowMs >= kKeepAfterMs && progress) {
      done_ = true;
      return Action::kKeep;
    }
    if (nowMs >= kGiveUpMs) {
      done_ = true;
      return Action::kRollBack;
    }
    return Action::kWait;
  }

  bool done() const { return done_; }

 private:
  bool started_ = false;
  bool done_ = false;
  uint32_t lastBeats_ = 0;
};

}  // namespace ledframe

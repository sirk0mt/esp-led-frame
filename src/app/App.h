// Application: creates the modules and runs the FreeRTOS render task.
//
// Threading model:
//   - The "render" task (core 1) owns EffectEngine and LedOutput. Nothing else
//     touches them.
//   - Other tasks (Arduino loop with the serial console now, network later) send
//     messages through a FreeRTOS queue with post(), setColorOrder() and
//     showTestPattern(). Messages are applied at frame boundaries.
//   - The render task publishes a StateSnapshot under a mutex. Readers copy it with
//     snapshot(). Its version grows whenever the state changes.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "app/RollbackGuard.h"
#include "app/StateSnapshot.h"
#include "core/WallTime.h"
#include "effects/Command.h"
#include "effects/Effect.h"
#include "effects/PaintImage.h"
#include "net/WifiManager.h"
#include "render/Layout.h"
#include "render/OutputMapping.h"
#include "state/Config.h"
#include "state/Presets.h"
#include "state/State.h"

namespace ledframe {

class EffectEngine;
class LedOutput;
class Palettes;

class App {
 public:
  // Called once from setup().
  void begin();
  // Called repeatedly from loop(): serial console (esp32dev only) and deferred
  // writes of /state.json, /config.json and /presets.json.
  void loop();

  // Thread-safe. Return false when the queue is full or the app is not running.
  bool post(const Command& command);
  bool setColorOrder(uint8_t order);
  // on = true shows the test pattern until showTestPattern(false) or the next state
  // command; a color order change keeps it on screen.
  bool showTestPattern(bool on = true);

  // Thread-safe copy of the latest state.
  StateSnapshot snapshot() const;

  // Presets, thread-safe. savePreset() and overwritePreset() store the active effect
  // with its parameters, the palette and the brightness from the latest snapshot.
  // savePreset() returns the id, or 0 with error set.
  uint8_t savePreset(const char* name, PresetError* error = nullptr);
  PresetError overwritePreset(uint8_t id);
  PresetError renamePreset(uint8_t id, const char* name);
  PresetError deletePreset(uint8_t id);
  // Queues an ApplyPreset command. Returns kNotFound for an unknown id and kFull
  // when the command queue is full.
  PresetError applyPreset(uint8_t id);
  void copyPresets(Presets& out) const;

  // Thread-safe. Stores the network in the configuration (written right away) and
  // asks WifiManager to connect. Returns false for a too long ssid or password.
  bool setWifiCredentials(const char* ssid, const char* password);
  WifiStatus wifiStatus() const;
  bool requestWifiScan();
  bool wifiScanRunning() const;
  uint8_t wifiScanResults(WifiNetwork* out, uint8_t capacity) const;

  // Thread-safe copy of the configuration, including secrets (never send them).
  Config config() const;
  // Thread-safe. Applies a validated configuration: layout, FPS, color order and
  // power limit go to the render task, Wi-Fi credentials to WifiManager, the rest
  // is stored (/config.json is written right away). A new hostname and LED data pin
  // take effect after a restart. Returns false when the command queue was full.
  bool applyConfig(const Config& next);
  // GPIO driving the strip since start; config().ledPin differs until a restart.
  uint8_t ledPin() const;

  // Thread-safe. Local time from the system clock in the configured time zone;
  // known is false until SNTP synchronized the clock.
  WallTime wallTime() const;
  // Thread-safe. True once SNTP was started and is running (for /api/info).
  bool sntpRunning() const;

  // Thread-safe. Writes pending files and restarts after delayMs, so the HTTP
  // response still goes out.
  void requestRestart(uint32_t delayMs = 500);

  // Palettes for GET /api/palettes. Only read after begin(): the user palette
  // colors are set once at start.
  const Palettes& palettes() const;

  // Paint image, thread-safe. postPaint() queues changes for the render task,
  // all or none; false when the queue has no room for all of them. copyPaint() writes
  // the image in the PaintImage binary format and returns the length, or 0 when out
  // is too small.
  bool postPaint(const PaintOp* ops, size_t count);
  size_t copyPaint(uint8_t* out, size_t capacity) const;

  // Live preview, thread-safe. While wanted, the render task copies the shown
  // canvas after each changed frame, at most every kPreviewIntervalMs (about 15 FPS).
  // copyPreview() writes the latest copy as a WsProtocol preview frame when its
  // version differs from version, then updates version; otherwise it returns 0.
  // out takes the frame's size, so a buffer that already has it never allocates.
  static constexpr uint32_t kPreviewIntervalMs = 66;
  void setPreviewWanted(bool wanted);
  size_t copyPreview(uint32_t& version, std::vector<uint8_t>& out) const;

  // Firmware update. beginOta() switches the LEDs off and pauses rendering (render
  // task) and asks the Arduino loop task to write pending files. cancelOta() resumes
  // rendering after a failed update. flushNow() writes pending files at once; call
  // it only from the Arduino loop task (ArduinoOTA runs there). All thread-safe.
  void beginOta();
  void cancelOta();
  void flushNow();

  // Rollback after an update (RollbackGuard.h). A firmware started for the first time
  // after an update is pending: a reset before it is confirmed starts the previous
  // firmware again. The Arduino loop task confirms it after RollbackGuard::kKeepAfterMs
  // of normal work, or rolls it back itself when the render task made no progress
  // until RollbackGuard::kGiveUpMs. confirmFirmware() confirms it at once and does nothing for a
  // firmware already confirmed; call it before a restart and before another update,
  // which writes over the previous firmware. Thread-safe.
  void confirmFirmware(const char* reason);

 private:
  enum class MessageType : uint8_t {
    kCommand,
    kColorOrder,
    kTestPattern,
    kFps,
    kPowerLimit,
    kOtaMode,  // value 1: LEDs off and rendering paused; 0: resume
    kDither,   // value 1: on, 0: off
    kGamma,    // value 1: on, 0: off
  };

  struct Message {
    MessageType type = MessageType::kCommand;
    uint8_t value = 0;    // kColorOrder, kFps, kPowerLimit (volts)
    uint32_t number = 0;  // kPowerLimit (milliamps)
    Command command;      // kCommand
  };

  bool postMessage(const Message& message);
  static void renderTaskEntry(void* arg);
  void renderTask();
  bool handle(const Message& message);
  void showCurrentFrame();
  // Keeps the paint image at the layout size and applies queued paint changes.
  void updatePaint();
  void updatePreview(uint32_t nowMs, bool shown);
  void publish(bool changed);
  void updateStats(uint32_t nowMs, bool frame, uint32_t renderUs, bool shown);
  TickType_t frameWait() const;

  void pollConsole();
  void runConsoleLine(const char* line);
  void printInfo() const;

  // Persistence (Arduino loop task): schedules deferred writes when the state
  // version or the configuration changes, and writes due files.
  void loadSettings(Config& config, State& state);
  void loadPaint(const LayoutConfig& layout);
  void persist();
  static bool produceState(void* context, IByteWriter& out);
  static bool produceConfig(void* context, IByteWriter& out);
  static bool producePresets(void* context, IByteWriter& out);
  static bool producePaint(void* context, IByteWriter& out);
  // Preset from the latest snapshot; false when no effect is active.
  bool currentPreset(const char* name, Preset& out) const;

  EffectEngine* engine_ = nullptr;
  LedOutput* output_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  QueueHandle_t paintQueue_ = nullptr;  // PaintOp items for the render task
  SemaphoreHandle_t mutex_ = nullptr;   // also guards sharedPaintImage()
  TaskHandle_t renderTask_ = nullptr;
  StateSnapshot snapshot_;  // guarded by mutex_
  Config config_;           // guarded by mutex_; engine fields refreshed by publish()
  State persisted_;         // guarded by mutex_; refreshed by publish()
  Presets presets_;         // guarded by mutex_

  // Arduino loop task only: what was last scheduled for writing.
  uint32_t savedVersion_ = 0;
  Config savedConfig_;
  uint32_t savedPresetsVersion_ = 0;
  uint32_t savedPaintVersion_ = 0;
  int stateSlot_ = -1;
  int configSlot_ = -1;
  int presetsSlot_ = -1;
  int paintSlot_ = -1;

  // Render task only.
  bool testMode_ = false;
  uint8_t paintEffect_ = 0xFF;  // registry index of "paint"
  uint32_t statsWindowStartMs_ = 0;
  uint32_t statsFrames_ = 0;
  uint32_t statsShown_ = 0;
  uint64_t statsRenderUs_ = 0;
  uint32_t statsRenderUsMax_ = 0;

  // Set by any task, handled by the Arduino loop task.
  volatile bool restartRequested_ = false;
  volatile uint32_t restartAtMs_ = 0;
  volatile bool flushRequested_ = false;

  // Render task only.
  bool otaMode_ = false;
  bool previewPending_ = false;  // a changed frame waits for the next copy
  uint32_t nextPreviewMs_ = 0;

  // Live preview copy, written by the render task under mutex_.
  CRGB previewPixels_[kMaxLeds];
  uint16_t previewWidth_ = 0;
  uint16_t previewHeight_ = 0;
  uint32_t previewVersion_ = 0;
  // Set by WsHub, read by the render task.
  volatile bool previewWanted_ = false;
  volatile bool previewForce_ = false;  // copy at once: a new subscriber needs a frame

  // Arduino loop task only: applies the time zone and starts SNTP once the station
  // is connected, again when the NTP server changes.
  void updateClock();
  uint32_t nextClockCheckMs_ = 0;
  volatile bool sntpStarted_ = false;                  // read by the web server task
  char clockTimezone_[Config::kTimezoneMax + 1] = {};  // the TZ rule in use
  // The server SNTP was started with. lwIP keeps this pointer, so it lives here.
  char clockServer_[Config::kNtpServerMax + 1] = {};

  // Arduino loop task only: switches the LEDs on or off at the scheduled times.
  void updateSchedule();
  uint32_t nextScheduleCheckMs_ = 0;
  ScheduleRunner scheduleRunner_;

  // Render task only: when the engine gets the wall time again (once per second).
  uint32_t nextWallTimeMs_ = 0;

  // Rollback: checkFirmware() at start, updateRollback() in the Arduino loop task.
  void checkFirmware();
  void updateRollback();
  std::atomic<bool> firmwarePending_{false};  // running firmware not confirmed yet
  volatile uint32_t renderBeats_ = 0;         // increased by the render task every loop
  uint32_t nextRollbackCheckMs_ = 0;          // Arduino loop task only
  RollbackGuard rollbackGuard_;               // Arduino loop task only

  // Arduino loop task only.
  void updateStatusOverride();
  WifiMode overrideMode_ = WifiMode::kOff;  // Wi-Fi mode the override was last set for
  char line_[128] = {};
  size_t lineLength_ = 0;
  bool lineOverflow_ = false;
};

}  // namespace ledframe

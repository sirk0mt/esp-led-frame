#include "app/App.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <lib8tion.h>

#include <cstdio>
#include <ctime>
#include <new>

#include "app/ConsoleParser.h"
#include "app/CrashReport.h"
#include "core/Log.h"
#include "core/Version.h"
#include "effects/EffectEngine.h"
#include "effects/EffectRegistry.h"
#include "net/Ota.h"
#include "net/WebServer.h"
#include "net/WsHub.h"
#include "net/WsProtocol.h"
#include "render/LedOutput.h"
#include "render/Palettes.h"
#include "state/Storage.h"

#include <cstring>

namespace ledframe {

namespace {

constexpr char kTag[] = "app";
constexpr char kConfigPath[] = "/config.json";
constexpr char kStatePath[] = "/state.json";
constexpr char kPresetsPath[] = "/presets.json";
constexpr char kPaintPath[] = "/paint.bin";
constexpr uint32_t kStateSaveDelayMs = 5000;  // written 5 s after the last change
constexpr uint32_t kConfigSaveDelayMs = 0;    // configuration is written right away
constexpr uint32_t kPresetsSaveDelayMs = 0;   // presets are written after every change
constexpr uint32_t kPaintSaveDelayMs = 5000;  // written 5 s after the last change
constexpr UBaseType_t kQueueLength = 16;
// Room for the largest paint message plus some more (about 4 KB).
constexpr UBaseType_t kPaintQueueLength = 384;
static_assert(kPaintQueueLength >= kPaintMaxPixels, "a full paint message must fit");
constexpr uint32_t kRenderStackBytes = 6144;
constexpr UBaseType_t kRenderPriority = 3;
constexpr BaseType_t kRenderCore = 1;
constexpr uint32_t kStatsWindowMs = 1000;
// Longest wait for the render task to pause for a flash write. It waits for messages
// at most one frame (100 ms at the lowest FPS), and a message wakes it at once.
constexpr uint32_t kRenderPauseWaitMs = 200;
constexpr uint32_t kWallTimeIntervalMs = 1000;  // the engine's wall time
constexpr uint32_t kClockCheckMs = 1000;        // time zone and SNTP start
constexpr uint32_t kScheduleCheckMs = 1000;     // on/off schedule
// A clock before 2025-01-01 has not been synchronized: the ESP32 starts at 1970.
constexpr time_t kMinValidEpoch = 1735689600;
constexpr unsigned long kSerialBaud = 115200;

uint32_t nowUs() {
  return static_cast<uint32_t>(esp_timer_get_time());
}

// Measures how long LedOutput::show() takes.
class TimingSink : public IFrameSink {
 public:
  explicit TimingSink(LedOutput& output) : output_(output) {}
  void show(const Frame& frame) override {
    const uint32_t start = nowUs();
    output_.show(frame);
    lastShowUs = nowUs() - start;
    showUsSum += lastShowUs;
    ++shows;
  }
  uint32_t lastShowUs = 0;
  uint64_t showUsSum = 0;
  uint32_t shows = 0;

 private:
  LedOutput& output_;
};

// The large modules live in static storage: about 13 KB for the engine and 3 KB for
// the LED buffer. The engine is constructed in App::begin(), after every effect has
// registered itself during static initialization.
Palettes sharedPalettes;
LedOutput ledOutput;
TimingSink timingSink(ledOutput);
alignas(EffectEngine) unsigned char engineStorage[sizeof(EffectEngine)];
Storage storage;
// Scratch copy for loading and serializing (about 2 KB), used only by setup() and the
// Arduino loop task, so it never sits on a task stack.
State scratchState;
// Same for presets (about 3.5 KB): loading, serializing and copies for the console.
Presets scratchPresets;
WifiManager wifi;
WebServer webServer;
WsHub wsHub;
Ota ota;
constexpr char kStatusEffectId[] = "status";

// A missing file is normal on a new device; a broken one is worth a warning.
void logLoad(const char* path, bool missing, const char* outcome) {
  if (missing) {
    LOG_I(kTag, "%s missing, %s", path, outcome);
  } else {
    LOG_W(kTag, "%s is broken, %s", path, outcome);
  }
}

bool sameLayout(const LayoutConfig& a, const LayoutConfig& b) {
  return a.width == b.width && a.height == b.height && a.serpentine == b.serpentine &&
         a.origin == b.origin && a.rotation == b.rotation && a.flipX == b.flipX &&
         a.flipY == b.flipY;
}

}  // namespace

void App::begin() {
  Serial.begin(kSerialBaud);
  LOG_I(kTag, "esp-led-frame %s (built %s)", kVersion, kBuildDate);
  captureCrashReport();
  checkFirmware();
  const EffectRegistry& registry = EffectRegistry::global();
  LOG_I(kTag, "%u effects registered", static_cast<unsigned>(registry.size()));
  registry.logIssues();
  // Effects use FastLED random8()/random16(); without a seed every boot repeats them.
  random16_set_seed(static_cast<uint16_t>(esp_random()));

  output_ = &ledOutput;
  engine_ = new (engineStorage) EffectEngine(registry, sharedPalettes, timingSink);

  queue_ = xQueueCreate(kQueueLength, sizeof(Message));
  paintQueue_ = xQueueCreate(kPaintQueueLength, sizeof(PaintOp));
  mutex_ = xSemaphoreCreateMutex();
  flashLock_ = xSemaphoreCreateMutex();
  renderPaused_ = xSemaphoreCreateBinary();
  if (queue_ == nullptr || paintQueue_ == nullptr || mutex_ == nullptr || flashLock_ == nullptr ||
      renderPaused_ == nullptr) {
    LOG_E(kTag, "cannot create queue or mutex");
    return;
  }
  const int paint = registry.indexOf("paint");
  paintEffect_ = paint >= 0 ? static_cast<uint8_t>(paint) : EffectEngine::kNoEffect;

  // Configuration and state from LittleFS; missing or broken files give defaults.
  storage.begin();
  Config config;
  loadSettings(config, scratchState);
  output_->begin(config.ledPin);  // The data pin is only read here: a new one needs a restart.
  output_->setColorOrder(config.colorOrder);
  output_->setPowerLimit(config.powerVolts, config.powerMilliamps);
  output_->setDither(config.dither);
  engine_->setGamma(config.gamma);
  engine_->setFps(config.fps);
  engine_->apply(Command::setLayout(config.layout));
  applyState(scratchState, registry, sharedPalettes, *engine_);
  loadPaint(config.layout);
  config_ = config;
  publish(true);

  // Nothing is written until something changes after start.
  savedVersion_ = snapshot_.version;
  savedConfig_ = config_;
  savedPresetsVersion_ = presets_.version();
  savedPaintVersion_ = sharedPaintImage().version();
  stateSlot_ = storage.registerFile(kStatePath, &App::produceState, this);
  configSlot_ = storage.registerFile(kConfigPath, &App::produceConfig, this);
  presetsSlot_ = storage.registerFile(kPresetsPath, &App::producePresets, this);
  paintSlot_ = storage.registerFile(kPaintPath, &App::producePaint, this);

  if (xTaskCreatePinnedToCore(&App::renderTaskEntry, "render", kRenderStackBytes, this,
                              kRenderPriority, &renderTask_, kRenderCore) != pdPASS) {
    LOG_E(kTag, "cannot start render task");
    renderTask_ = nullptr;
    return;
  }

  // Wi-Fi last: it is non-blocking, and the render task already shows the effect.
  std::memcpy(hostname_, config.hostname, sizeof(hostname_));
  wifi.begin(config.hostname, config.wifiSsid, config.wifiPassword);
  webServer.begin(*this, wsHub, ota);  // After WiFi.mode(): the TCP stack must be running.
  ota.beginArduinoOta(*this, config.hostname, config.otaPassword);
#if defined(LEDFRAME_CONSOLE)
  std::printf("%s\n", kConsoleHelp);
#endif
}

void App::loop() {
#if defined(LEDFRAME_CONSOLE)
  pollConsole();
#endif
  wifi.tick(millis());
  updateStatusOverride();
  updateClock();
  updateSchedule();
  wsHub.tick(millis());
  ota.tick();
  persist();
  updateRollback();
  if (flushRequested_) {
    flushRequested_ = false;
    flushNow();
  }
  if (restartRequested_ && static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
    flushNow();  // Pending state and presets reach the flash first.
    // A restart asked for over HTTP or the console shows the firmware works; without
    // this the restart would start the previous firmware again.
    confirmFirmware("restart requested");
    LOG_I(kTag, "restarting");
    ESP.restart();
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}

void App::requestRestart(uint32_t delayMs) {
  restartAtMs_ = millis() + delayMs;
  restartRequested_ = true;
}

// The bootloader starts a new firmware once as "pending verify" and goes back to the
// previous one when the device resets before the firmware is confirmed. Arduino
// would confirm it right away; verifyRollbackLater() in main.cpp leaves that to
// updateRollback().
void App::checkFirmware() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    firmwarePending_ = true;
    LOG_I(kTag,
          "new firmware in %s, kept after %u s of normal work "
          "(a reset before that brings back the previous one)",
          running->label, static_cast<unsigned>(RollbackGuard::kKeepAfterMs / 1000));
  } else if (running != nullptr) {
    LOG_I(kTag, "firmware in %s", running->label);
  }
  // Partition of an update that was undone. A later update into it clears this.
  const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
  if (invalid != nullptr && invalid != running) {
    LOG_W(kTag, "the update in %s did not work and was rolled back", invalid->label);
  }
}

// Once per second: keeps or undoes a pending firmware when RollbackGuard says so.
void App::updateRollback() {
  if (!firmwarePending_ || static_cast<int32_t>(millis() - nextRollbackCheckMs_) < 0) {
    return;
  }
  nextRollbackCheckMs_ = millis() + 1000;
  switch (rollbackGuard_.update(millis(), renderBeats_)) {
    case RollbackGuard::Action::kWait:
      return;
    case RollbackGuard::Action::kKeep:
      confirmFirmware("running normally");
      return;
    case RollbackGuard::Action::kRollBack:
      // Another task may have kept it in the meantime (restart, update).
      if (firmwarePending_.exchange(false)) {
        LOG_E(kTag, "render task stopped, rolling the update back");
        flushNow();
        // Restarts into the previous firmware; returns only on an error.
        const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
        LOG_E(kTag, "cannot roll back: %s", esp_err_to_name(result));
      }
      return;
  }
}

void App::confirmFirmware(const char* reason) {
  if (!firmwarePending_.exchange(false)) {
    return;
  }
  esp_err_t result = ESP_OK;
  {
    FlashWrite write(*this);  // The app state lives in the flash (otadata).
    result = esp_ota_mark_app_valid_cancel_rollback();
  }
  if (result == ESP_OK) {
    LOG_I(kTag, "new firmware kept (%s)", reason);
  } else {
    LOG_E(kTag, "cannot keep the new firmware: %s", esp_err_to_name(result));
  }
}

// Asks the render task to stop between two frames and waits until it does. A render
// task stuck elsewhere does not block the write for long: after kRenderPauseWaitMs the
// write goes ahead and may corrupt a frame.
void App::beginFlashWrite() {
  if (flashLock_ == nullptr) {
    return;
  }
  xSemaphoreTake(flashLock_, portMAX_DELAY);
  if (renderTask_ == nullptr) {
    return;  // Before the render task starts nothing is sent to the strip.
  }
  xSemaphoreTake(renderPaused_, 0);  // A late answer to an earlier write.
  flashWriting_ = true;
  Message message;
  message.type = MessageType::kFlashWrite;
  postMessage(message);  // Wakes it at once; a full queue wakes it anyway.
  if (xSemaphoreTake(renderPaused_, pdMS_TO_TICKS(kRenderPauseWaitMs)) != pdTRUE) {
    LOG_W(kTag, "render task did not pause, writing the flash anyway");
  }
}

void App::endFlashWrite() {
  if (flashLock_ == nullptr) {
    return;
  }
  flashWriting_ = false;
  xSemaphoreGive(flashLock_);
}

// Render task, between two frames: no frame is being sent now (FastLED.show() returns
// once it is out), and none is started until the flash write is done.
void App::pauseForFlashWrite() {
  xSemaphoreGive(renderPaused_);
  while (flashWriting_) {
    vTaskDelay(1);
  }
}

Config App::config() const {
  Config copy;
  if (mutex_ == nullptr) {
    return copy;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  copy = config_;
  xSemaphoreGive(mutex_);
  return copy;
}

uint8_t App::ledPin() const {
  return output_ != nullptr ? output_->pin() : kDefaultLedPin;
}

bool App::applyConfig(const Config& next) {
  if (mutex_ == nullptr) {
    return false;
  }
  // Fields without an engine counterpart are stored right away. Layout, FPS and
  // color order reach config_ through publish() once the render task applied them.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const Config current = config_;
  config_.ledPin = next.ledPin;  // used from the next start
  config_.powerVolts = next.powerVolts;
  config_.powerMilliamps = next.powerMilliamps;
  config_.dither = next.dither;
  config_.gamma = next.gamma;
  std::memcpy(config_.hostname, next.hostname, sizeof(config_.hostname));
  std::memcpy(config_.wifiSsid, next.wifiSsid, sizeof(config_.wifiSsid));
  std::memcpy(config_.wifiPassword, next.wifiPassword, sizeof(config_.wifiPassword));
  std::memcpy(config_.otaPassword, next.otaPassword, sizeof(config_.otaPassword));
  // The time zone and the NTP server take effect in updateClock() within a second.
  std::memcpy(config_.timezone, next.timezone, sizeof(config_.timezone));
  std::memcpy(config_.ntpServer, next.ntpServer, sizeof(config_.ntpServer));
  config_.schedule = next.schedule;
  xSemaphoreGive(mutex_);

  bool queued = true;
  if (!sameLayout(next.layout, current.layout)) {
    queued &= post(Command::setLayout(next.layout));
  }
  if (next.colorOrder != current.colorOrder) {
    queued &= setColorOrder(next.colorOrder);
  }
  if (next.fps != current.fps) {
    Message message;
    message.type = MessageType::kFps;
    message.value = next.fps;
    queued &= postMessage(message);
  }
  if (next.powerVolts != current.powerVolts || next.powerMilliamps != current.powerMilliamps) {
    Message message;
    message.type = MessageType::kPowerLimit;
    message.value = next.powerVolts;
    message.number = next.powerMilliamps;
    queued &= postMessage(message);
  }
  if (next.dither != current.dither) {
    Message message;
    message.type = MessageType::kDither;
    message.value = next.dither ? 1 : 0;
    queued &= postMessage(message);
  }
  if (next.gamma != current.gamma) {
    Message message;
    message.type = MessageType::kGamma;
    message.value = next.gamma ? 1 : 0;
    queued &= postMessage(message);
  }
  if (std::strcmp(next.wifiSsid, current.wifiSsid) != 0 ||
      std::strcmp(next.wifiPassword, current.wifiPassword) != 0) {
    wifi.requestConnect(next.wifiSsid, next.wifiPassword);
  }
  if (std::strcmp(next.hostname, current.hostname) != 0) {
    LOG_I(kTag, "new hostname '%s' applies after a restart", next.hostname);
  }
  if (next.ledPin != current.ledPin) {
    LOG_I(kTag, "new LED data pin GPIO %u applies after a restart",
          static_cast<unsigned>(next.ledPin));
  }
  return queued;
}

WallTime App::wallTime() const {
  WallTime out;
  const time_t now = std::time(nullptr);
  if (now < kMinValidEpoch || mutex_ == nullptr) {
    return out;
  }
  struct tm local = {};
  // localtime_r reads TZ, which updateClock() changes: newlib's environment is not
  // thread-safe, so both run under the mutex.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  localtime_r(&now, &local);
  xSemaphoreGive(mutex_);
  out.known = true;
  out.year = static_cast<uint16_t>(local.tm_year + 1900);
  out.month = static_cast<uint8_t>(local.tm_mon + 1);
  out.day = static_cast<uint8_t>(local.tm_mday);
  out.hour = static_cast<uint8_t>(local.tm_hour);
  out.minute = static_cast<uint8_t>(local.tm_min);
  out.second = static_cast<uint8_t>(local.tm_sec);
  out.weekday = static_cast<uint8_t>((local.tm_wday + 6) % 7);  // struct tm: 0 = Sunday
  return out;
}

bool App::sntpRunning() const {
  return sntpStarted_ && esp_sntp_enabled();
}

void App::updateClock() {
  if (static_cast<int32_t>(millis() - nextClockCheckMs_) < 0 || mutex_ == nullptr) {
    return;
  }
  nextClockCheckMs_ = millis() + kClockCheckMs;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool newZone = std::strcmp(config_.timezone, clockTimezone_) != 0;
  if (newZone) {
    std::memcpy(clockTimezone_, config_.timezone, sizeof(clockTimezone_));
    setenv("TZ", clockTimezone_, 1);
    tzset();
  }
  const bool newServer = std::strcmp(config_.ntpServer, clockServer_) != 0;
  if (newServer) {
    std::memcpy(clockServer_, config_.ntpServer, sizeof(clockServer_));
  }
  const bool start = wifi.status().mode == WifiMode::kConnected && (!sntpStarted_ || newServer);
  if (start) {
    configTzTime(clockTimezone_, clockServer_);  // also sets TZ, so under the mutex too
    sntpStarted_ = true;
  }
  xSemaphoreGive(mutex_);
  if (newZone) {
    LOG_I(kTag, "time zone %s", clockTimezone_);
  }
  if (start) {
    LOG_I(kTag, "SNTP from %s", clockServer_);
  }
}

void App::updateSchedule() {
  if (static_cast<int32_t>(millis() - nextScheduleCheckMs_) < 0 || mutex_ == nullptr) {
    return;
  }
  nextScheduleCheckMs_ = millis() + kScheduleCheckMs;
  const WallTime now = wallTime();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const Schedule schedule = config_.schedule;
  xSemaphoreGive(mutex_);
  const ScheduleRunner::Action action = scheduleRunner_.update(schedule, now);
  if (action == ScheduleRunner::Action::kNone) {
    return;
  }
  // The same command as the on/off switch in the UI: the state is saved and sent to
  // every page. A full queue skips this minute; the log tells.
  const bool on = action == ScheduleRunner::Action::kOn;
  if (post(Command::setPower(on))) {
    LOG_I(kTag, "schedule: %s at %02u:%02u", on ? "on" : "off", static_cast<unsigned>(now.hour),
          static_cast<unsigned>(now.minute));
  } else {
    LOG_W(kTag, "schedule: command queue full, %s skipped", on ? "on" : "off");
  }
}

const Palettes& App::palettes() const {
  return sharedPalettes;
}

void App::beginOta() {
  Message message;
  message.type = MessageType::kOtaMode;
  message.value = 1;
  postMessage(message);
  flushRequested_ = true;
}

void App::cancelOta() {
  Message message;
  message.type = MessageType::kOtaMode;
  message.value = 0;
  postMessage(message);
}

void App::flushNow() {
  if (!storage.pending()) {
    return;
  }
  FlashWrite write(*this);
  storage.flushAll();
}

// The hidden status effect is shown while the device runs its own access point and
// removed once the station connects. A failed post is retried in the next loop.
void App::updateStatusOverride() {
  const WifiMode mode = wifi.status().mode;
  if (mode == overrideMode_) {
    return;
  }
  const bool wasAp = overrideMode_ == WifiMode::kAccessPoint;
  const bool isAp = mode == WifiMode::kAccessPoint;
  if (isAp != wasAp) {
    const int status = EffectRegistry::global().indexOf(kStatusEffectId);
    const uint8_t target = isAp && status >= 0 ? static_cast<uint8_t>(status)
                                               : static_cast<uint8_t>(Command::kNoOverride);
    if (!post(Command::setOverride(target))) {
      return;
    }
  }
  overrideMode_ = mode;
}

bool App::setWifiCredentials(const char* ssid, const char* password) {
  if (mutex_ == nullptr || ssid == nullptr || password == nullptr ||
      std::strlen(ssid) > Config::kSsidMax || std::strlen(password) > Config::kPasswordMax) {
    return false;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::snprintf(config_.wifiSsid, sizeof(config_.wifiSsid), "%s", ssid);
  std::snprintf(config_.wifiPassword, sizeof(config_.wifiPassword), "%s", password);
  xSemaphoreGive(mutex_);
  wifi.requestConnect(ssid, password);
  return true;
}

WifiStatus App::wifiStatus() const {
  return wifi.status();
}

bool App::requestWifiScan() {
  return wifi.requestScan();
}

bool App::wifiScanRunning() const {
  return wifi.scanRunning();
}

uint8_t App::wifiScanResults(WifiNetwork* out, uint8_t capacity) const {
  return wifi.scanResults(out, capacity);
}

// A file that is missing or broken gives the defaults. A file too large to read never
// reaches its parser, so the defaults are set here too.
void App::loadSettings(Config& config, State& state) {
  using Result = Storage::ReadResult;
  const EffectRegistry& registry = EffectRegistry::global();
  Result result = storage.read(
      kConfigPath,
      [](void* out, IByteReader& in, size_t) {
        return deserializeConfig(in, *static_cast<Config*>(out));
      },
      &config);
  if (result != Result::kOk) {
    config = Config{};
    logLoad(kConfigPath, result == Result::kMissing, "using defaults");
  }

  result = storage.read(
      kStatePath,
      [](void* out, IByteReader& in, size_t) {
        return deserializeState(in, EffectRegistry::global(), *static_cast<State*>(out));
      },
      &state);
  if (result != Result::kOk) {
    state.reset(registry);
    logLoad(kStatePath, result == Result::kMissing, "using defaults");
  }

  // Presets go straight into presets_: the render task is not running yet.
  result = storage.read(
      kPresetsPath,
      [](void* out, IByteReader& in, size_t) {
        return deserializePresets(in, EffectRegistry::global(), *static_cast<Presets*>(out));
      },
      &presets_);
  if (result != Result::kOk) {
    presets_.clear();
    logLoad(kPresetsPath, result == Result::kMissing, "no presets");
  } else {
    LOG_I(kTag, "%u presets loaded", static_cast<unsigned>(presets_.count()));
  }
}

// The picture follows the layout size: a stored picture of another size keeps its
// top-left part.
void App::loadPaint(const LayoutConfig& layout) {
  PaintImage& image = sharedPaintImage();
  const Storage::ReadResult result = storage.read(
      kPaintPath,
      [](void* out, IByteReader& in, size_t length) {
        return static_cast<PaintImage*>(out)->deserialize(in, length);
      },
      &image);
  if (result != Storage::ReadResult::kOk) {
    logLoad(kPaintPath, result == Storage::ReadResult::kMissing, "empty picture");
  }
  image.resize(layout.width, layout.height);
}

void App::persist() {
  if (mutex_ == nullptr) {
    return;
  }
  const uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t version = snapshot_.version;
  const Config config = config_;
  const uint32_t presetsVersion = presets_.version();
  const uint32_t paintVersion = sharedPaintImage().version();
  xSemaphoreGive(mutex_);

  if (version != savedVersion_) {
    savedVersion_ = version;
    storage.schedule(stateSlot_, now, kStateSaveDelayMs);  // Each change pushes the write.
  }
  if (config != savedConfig_) {
    savedConfig_ = config;
    storage.schedule(configSlot_, now, kConfigSaveDelayMs);
  }
  if (presetsVersion != savedPresetsVersion_) {
    savedPresetsVersion_ = presetsVersion;
    storage.schedule(presetsSlot_, now, kPresetsSaveDelayMs);
  }
  if (paintVersion != savedPaintVersion_) {
    savedPaintVersion_ = paintVersion;
    storage.schedule(paintSlot_, now, kPaintSaveDelayMs);  // Each stroke pushes the write.
  }
  if (storage.due(now)) {
    FlashWrite write(*this);
    storage.tick(now);
  }
}

bool App::produceState(void* context, IByteWriter& out) {
  App& app = *static_cast<App*>(context);
  xSemaphoreTake(app.mutex_, portMAX_DELAY);
  scratchState = app.persisted_;
  xSemaphoreGive(app.mutex_);
  return serializeState(scratchState, EffectRegistry::global(), out) > 0;
}

bool App::produceConfig(void* context, IByteWriter& out) {
  App& app = *static_cast<App*>(context);
  xSemaphoreTake(app.mutex_, portMAX_DELAY);
  const Config config = app.config_;
  xSemaphoreGive(app.mutex_);
  return serializeConfig(config, out) > 0;
}

bool App::producePresets(void* context, IByteWriter& out) {
  static_cast<App*>(context)->copyPresets(scratchPresets);
  return serializePresets(scratchPresets, EffectRegistry::global(), out) > 0;
}

// The picture is copied in small parts under the mutex, so the render task never
// waits for the flash. A change between two parts would mix two pictures: the write
// is abandoned, and the change schedules a new one (persist()).
bool App::producePaint(void* context, IByteWriter& out) {
  App& app = *static_cast<App*>(context);
  const PaintImage& image = sharedPaintImage();
  uint8_t part[96];
  size_t offset = 0;
  uint32_t version = 0;
  for (;;) {
    xSemaphoreTake(app.mutex_, portMAX_DELAY);
    if (offset == 0) {
      version = image.version();
    }
    const bool changed = image.version() != version;
    const size_t count = changed ? 0 : image.serializePart(offset, part, sizeof(part));
    xSemaphoreGive(app.mutex_);
    if (changed) {
      LOG_D(kTag, "picture changed while saving, saved later");
      return false;
    }
    if (count == 0) {
      return offset > 0;
    }
    if (out.write(part, count) != count) {
      return false;
    }
    offset += count;
  }
}

bool App::postPaint(const PaintOp* ops, size_t count) {
  if (paintQueue_ == nullptr || renderTask_ == nullptr) {
    return false;
  }
  // Only the WebSocket handler sends paint changes, so the room cannot shrink
  // between this check and the sends.
  if (uxQueueSpacesAvailable(paintQueue_) < count) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    xQueueSend(paintQueue_, &ops[i], 0);
  }
  return true;
}

size_t App::copyPaint(uint8_t* out, size_t capacity) const {
  if (mutex_ == nullptr) {
    return 0;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t length = sharedPaintImage().serialize(out, capacity);
  xSemaphoreGive(mutex_);
  return length;
}

bool App::currentPreset(const char* name, Preset& out) const {
  const StateSnapshot state = snapshot();
  if (state.effect >= EffectRegistry::global().size()) {
    return false;
  }
  out = makePreset(name, state.effect, state.params, state.paramCount, state.palette,
                   state.brightness);
  return true;
}

uint8_t App::savePreset(const char* name, PresetError* error) {
  Preset preset;
  if (mutex_ == nullptr || !currentPreset(name, preset)) {
    if (error != nullptr) {
      *error = PresetError::kInvalidEffect;
    }
    return 0;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint8_t id = presets_.save(preset, error);
  xSemaphoreGive(mutex_);
  return id;
}

PresetError App::overwritePreset(uint8_t id) {
  Preset preset;
  if (mutex_ == nullptr || !currentPreset("", preset)) {
    return PresetError::kInvalidEffect;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const PresetError result = presets_.overwrite(id, preset);
  xSemaphoreGive(mutex_);
  return result;
}

PresetError App::renamePreset(uint8_t id, const char* name) {
  if (mutex_ == nullptr) {
    return PresetError::kNotFound;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const PresetError result = presets_.rename(id, name);
  xSemaphoreGive(mutex_);
  return result;
}

PresetError App::deletePreset(uint8_t id) {
  if (mutex_ == nullptr) {
    return PresetError::kNotFound;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const PresetError result = presets_.remove(id);
  xSemaphoreGive(mutex_);
  return result;
}

PresetError App::applyPreset(uint8_t id) {
  if (mutex_ == nullptr) {
    return PresetError::kNotFound;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const Preset* preset = presets_.find(id);
  Command command;
  if (preset != nullptr) {
    command = preset->toCommand();
  }
  xSemaphoreGive(mutex_);
  if (preset == nullptr) {
    return PresetError::kNotFound;
  }
  return post(command) ? PresetError::kNone : PresetError::kFull;
}

void App::copyPresets(Presets& out) const {
  if (mutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  out = presets_;
  xSemaphoreGive(mutex_);
}

bool App::post(const Command& command) {
  Message message;
  message.type = MessageType::kCommand;
  message.command = command;
  return postMessage(message);
}

bool App::setColorOrder(uint8_t order) {
  if (!isValidColorOrder(order)) {
    return false;
  }
  Message message;
  message.type = MessageType::kColorOrder;
  message.value = order;
  return postMessage(message);
}

bool App::showTestPattern(bool on) {
  Message message;
  message.type = MessageType::kTestPattern;
  message.value = on ? 1 : 0;
  return postMessage(message);
}

StateSnapshot App::snapshot() const {
  StateSnapshot copy;
  if (mutex_ == nullptr) {
    return copy;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  copy = snapshot_;
  xSemaphoreGive(mutex_);
  return copy;
}

bool App::postMessage(const Message& message) {
  if (queue_ == nullptr || renderTask_ == nullptr) {
    return false;
  }
  return xQueueSend(queue_, &message, 0) == pdTRUE;
}

void App::renderTaskEntry(void* arg) {
  static_cast<App*>(arg)->renderTask();
}

void App::renderTask() {
  statsWindowStartMs_ = millis();
  for (;;) {
    renderBeats_ = renderBeats_ + 1;  // Progress for the rollback check (RollbackGuard).
    // 1. Wait for the next frame. A message wakes the task earlier: it is applied
    //    at once, between two frames, and the frame still comes on schedule.
    bool changed = false;
    Message message;
    if (xQueueReceive(queue_, &message, frameWait()) == pdTRUE) {
      do {
        changed |= handle(message);
      } while (xQueueReceive(queue_, &message, 0) == pdTRUE);
    }
    if (flashWriting_) {
      pauseForFlashWrite();  // Between two frames, until the flash write is done.
    }
    updatePaint();  // After the commands: a new layout resizes the picture first.
    if (static_cast<int32_t>(millis() - nextWallTimeMs_) >= 0) {
      nextWallTimeMs_ = millis() + kWallTimeIntervalMs;
      engine_->setWallTime(wallTime());
    }

    // 2. Render and show when the engine says a frame is due, unless the test
    //    pattern is on screen.
    bool frame = false;
    bool shown = false;
    uint32_t renderUs = 0;
    if (!testMode_ && !otaMode_) {
      const uint32_t framesBefore = engine_->frames();
      const uint32_t showsBefore = timingSink.shows;
      const uint32_t start = nowUs();
      shown = engine_->tick(millis());
      const uint32_t total = nowUs() - start;
      const uint32_t showUs = timingSink.shows != showsBefore ? timingSink.lastShowUs : 0;
      renderUs = total > showUs ? total - showUs : 0;
      frame = engine_->frames() != framesBefore;
    }

    // 3. Publish the state for other tasks.
    if (changed) {
      publish(true);
    }
    updatePreview(millis(), shown);
    updateStats(millis(), frame, renderUs, shown);
  }
}

// Ticks until the engine's next frame. While the test pattern or an update holds the
// LEDs nothing is rendered, so the task only waits for messages, a frame time at once.
TickType_t App::frameWait() const {
  if (testMode_ || otaMode_) {
    return pdMS_TO_TICKS(1000u / engine_->fps());
  }
  const int32_t waitMs = static_cast<int32_t>(engine_->nextFrameMs() - millis());
  return waitMs > 0 ? pdMS_TO_TICKS(static_cast<uint32_t>(waitMs)) : 0;
}

bool App::handle(const Message& message) {
  switch (message.type) {
    case MessageType::kCommand: {
      const bool changed = engine_->apply(message.command);
      // The status override is not part of the saved state, so it does not change
      // the snapshot version (no write of /state.json) and keeps the test pattern.
      if (message.command.type == CommandType::SetOverride) {
        return false;
      }
      if (testMode_) {
        // Any command other than "order" leaves the test pattern.
        testMode_ = false;
        showCurrentFrame();
        return true;
      }
      return changed;
    }
    case MessageType::kColorOrder:
      if (!output_->setColorOrder(message.value)) {
        return false;
      }
      if (testMode_) {
        output_->showTestPattern(engine_->layout().size(), engine_->brightness());
      } else {
        showCurrentFrame();  // The engine does not resend an unchanged frame.
      }
      return true;
    case MessageType::kTestPattern:
      if (message.value == 0) {
        if (!testMode_) {
          return false;
        }
        testMode_ = false;
        showCurrentFrame();
        return true;
      }
      testMode_ = true;
      output_->showTestPattern(engine_->layout().size(), engine_->brightness());
      return true;
    case MessageType::kFps: {
      const uint8_t before = engine_->fps();
      engine_->setFps(message.value);
      return engine_->fps() != before;  // publish() copies the FPS into config_.
    }
    case MessageType::kPowerLimit:
      output_->setPowerLimit(message.value, message.number);
      return false;  // config_ already holds the new limit.
    case MessageType::kDither:
      output_->setDither(message.value != 0);
      if (!testMode_ && !otaMode_) {
        showCurrentFrame();  // A static frame would keep the old dithering.
      }
      return false;  // config_ already holds the new setting.
    case MessageType::kGamma:
      engine_->setGamma(message.value != 0);  // The engine draws the frame again.
      return false;                           // config_ already holds the new setting.
    case MessageType::kFlashWrite:
      return false;  // Only wakes the task; renderTask() pauses after the messages.
    case MessageType::kOtaMode:
      otaMode_ = message.value != 0;
      if (otaMode_) {
        // Black frame: brightness 0 switches every LED off.
        timingSink.show(Frame{engine_->output(), engine_->layout(), 0});
      } else {
        showCurrentFrame();
      }
      return false;
  }
  return false;
}

void App::updatePaint() {
  PaintImage& image = sharedPaintImage();
  const Layout& layout = engine_->layout();
  const bool resize = image.width() != layout.width() || image.height() != layout.height();
  if (!resize && uxQueueMessagesWaiting(paintQueue_) == 0) {
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t before = image.version();
  if (resize) {
    image.resize(layout.width(), layout.height());
  }
  PaintOp op;
  while (xQueueReceive(paintQueue_, &op, 0) == pdTRUE) {
    image.apply(op);  // A pixel outside a just shrunk picture is ignored.
  }
  const bool changed = image.version() != before;
  xSemaphoreGive(mutex_);
  if (changed && engine_->activeEffect() == paintEffect_) {
    engine_->redraw();
  }
}

// A changed frame is copied at the next preview time, so the last frame of an
// animation that stops still arrives.
void App::updatePreview(uint32_t nowMs, bool shown) {
  if (!previewWanted_) {
    previewPending_ = false;
    return;
  }
  previewPending_ |= shown;
  const bool force = previewForce_;
  if (!force && (!previewPending_ || static_cast<int32_t>(nowMs - nextPreviewMs_) < 0)) {
    return;
  }
  previewForce_ = false;
  previewPending_ = false;
  // A fixed schedule: with 20 ms frames, "now + 66 ms" would wait for the 80 ms frame
  // every time and give 12.5 instead of 15 FPS. After a pause it starts from now.
  nextPreviewMs_ += kPreviewIntervalMs;
  if (static_cast<int32_t>(nowMs - nextPreviewMs_) >= 0) {
    nextPreviewMs_ = nowMs + kPreviewIntervalMs;
  }
  const Canvas& canvas = engine_->output();
  const bool on = engine_->on();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  previewWidth_ = canvas.width();
  previewHeight_ = canvas.height();
  for (uint16_t i = 0; i < canvas.size(); ++i) {
    previewPixels_[i] = on ? canvas[i] : CRGB(CRGB::Black);
  }
  ++previewVersion_;
  xSemaphoreGive(mutex_);
}

void App::setPreviewWanted(bool wanted) {
  if (wanted && !previewWanted_) {
    previewForce_ = true;  // The copy may be old: it was not updated without viewers.
  }
  previewWanted_ = wanted;
}

size_t App::copyPreview(uint32_t& version, std::vector<uint8_t>& out) const {
  if (mutex_ == nullptr) {
    return 0;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t length = 0;
  if (previewVersion_ != version) {
    out.resize(kPreviewHeaderSize + static_cast<size_t>(previewWidth_) * previewHeight_ * 3);
    length =
        writePreviewFrame(previewPixels_, previewWidth_, previewHeight_, out.data(), out.size());
    if (length > 0) {
      version = previewVersion_;
    }
  }
  xSemaphoreGive(mutex_);
  return length;
}

void App::showCurrentFrame() {
  const uint8_t brightness = engine_->on() ? engine_->brightness() : 0;
  timingSink.show(Frame{engine_->output(), engine_->layout(), brightness});
}

void App::publish(bool changed) {
  StateSnapshot next;
  next.on = engine_->on();
  next.brightness = engine_->brightness();
  next.effect = engine_->activeEffect();
  next.palette = engine_->paletteIndex();
  next.transitionMs = engine_->transitionMs();
  const EffectDescriptor* descriptor = engine_->activeDescriptor();
  if (descriptor != nullptr) {
    next.paramCount = descriptor->paramCount;
    const int32_t* values = engine_->params(next.effect);
    for (uint8_t i = 0; i < next.paramCount; ++i) {
      next.params[i] = values[i];
    }
  }
  next.colorOrder = output_->colorOrder();
  next.layout = engine_->layout().config();
  next.fps = engine_->fps();
  next.testPattern = testMode_;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  next.version = snapshot_.version + (changed ? 1 : 0);
  next.stats = snapshot_.stats;
  snapshot_ = next;
  // Persisted copies: captured in place to keep the 2 KB State off the task stack.
  config_.layout = next.layout;
  config_.colorOrder = static_cast<uint8_t>(next.colorOrder);
  config_.fps = next.fps;
  captureState(*engine_, EffectRegistry::global(), sharedPalettes, persisted_);
  xSemaphoreGive(mutex_);
}

void App::updateStats(uint32_t nowMs, bool frame, uint32_t renderUs, bool shown) {
  if (frame) {
    ++statsFrames_;
    statsRenderUs_ += renderUs;
    if (renderUs > statsRenderUsMax_) {
      statsRenderUsMax_ = renderUs;
    }
  }
  if (shown) {
    ++statsShown_;
  }
  const uint32_t elapsed = nowMs - statsWindowStartMs_;
  if (elapsed < kStatsWindowMs) {
    return;
  }

  EngineStats stats;
  stats.frameFps = static_cast<uint16_t>((statsFrames_ * 1000u + elapsed / 2) / elapsed);
  stats.shownFps = static_cast<uint16_t>((statsShown_ * 1000u + elapsed / 2) / elapsed);
  stats.renderUsAvg = statsFrames_ > 0 ? static_cast<uint32_t>(statsRenderUs_ / statsFrames_) : 0;
  stats.renderUsMax = statsRenderUsMax_;
  stats.showUsAvg =
      timingSink.shows > 0 ? static_cast<uint32_t>(timingSink.showUsSum / timingSink.shows) : 0;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  snapshot_.stats = stats;
  xSemaphoreGive(mutex_);

  statsWindowStartMs_ = nowMs;
  statsFrames_ = 0;
  statsShown_ = 0;
  statsRenderUs_ = 0;
  statsRenderUsMax_ = 0;
  timingSink.showUsSum = 0;
  timingSink.shows = 0;
}

#if defined(LEDFRAME_CONSOLE)

void App::pollConsole() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) {
      return;
    }
    if (c == '\n' || c == '\r') {
      if (lineOverflow_) {
        std::printf("error: line too long\n");
      } else if (lineLength_ > 0) {
        line_[lineLength_] = '\0';
        runConsoleLine(line_);
      }
      lineLength_ = 0;
      lineOverflow_ = false;
      continue;
    }
    static_assert(sizeof(line_) > kConsoleLineMax, "console buffer shorter than a line");
    if (lineLength_ < kConsoleLineMax) {
      line_[lineLength_++] = static_cast<char>(c);
    } else {
      lineOverflow_ = true;
    }
  }
}

void App::runConsoleLine(const char* line) {
  const StateSnapshot state = snapshot();
  const ConsoleResult result = parseConsoleLine(line, EffectRegistry::global(), state.effect);
  bool queued = true;
  switch (result.action) {
    case ConsoleAction::kNone:
      return;
    case ConsoleAction::kEngine:
      queued = post(result.command);
      break;
    case ConsoleAction::kColorOrder:
      queued = setColorOrder(result.colorOrder);
      break;
    case ConsoleAction::kTestPattern:
      queued = showTestPattern();
      break;
    case ConsoleAction::kInfo:
      printInfo();
      return;
    case ConsoleAction::kPresetSave: {
      PresetError error = PresetError::kNone;
      const uint8_t id = savePreset(result.presetName, &error);
      if (id != 0) {
        std::printf("ok, preset %u\n", static_cast<unsigned>(id));
      } else {
        std::printf("error: %s\n", error == PresetError::kFull          ? "preset list full"
                                   : error == PresetError::kInvalidName ? "invalid preset name"
                                                                        : "no active effect");
      }
      return;
    }
    case ConsoleAction::kPresetApply: {
      const PresetError error = applyPreset(result.presetId);
      std::printf(error == PresetError::kNone       ? "ok\n"
                  : error == PresetError::kNotFound ? "error: unknown preset\n"
                                                    : "error: command queue full\n");
      return;
    }
    case ConsoleAction::kWifi:
      queued = setWifiCredentials(result.wifiSsid, result.wifiPassword);
      std::printf(queued ? "ok, connecting to '%s'\n" : "error: invalid network '%s'\n",
                  result.wifiSsid);
      return;
    case ConsoleAction::kError:
      std::printf("error: %s\n%s\n", result.error, kConsoleHelp);
      return;
  }
  std::printf(queued ? "ok\n" : "error: command queue full\n");
}

void App::printInfo() const {
  const StateSnapshot s = snapshot();
  const EffectDescriptor* effect = EffectRegistry::global().at(s.effect);
  std::printf("esp-led-frame %s (built %s)\n", kVersion, kBuildDate);
  std::printf("uptime %lu s, free heap %lu B (min %lu B)\n",
              static_cast<unsigned long>(millis() / 1000),
              static_cast<unsigned long>(ESP.getFreeHeap()),
              static_cast<unsigned long>(ESP.getMinFreeHeap()));
  const CrashInfo& crash = crashReport();
  std::printf("reset reason %s", crash.resetReason);
  if (crash.hasCoredump) {
    std::printf(", last crash in task '%s' at PC 0x%08lX", crash.task,
                static_cast<unsigned long>(crash.pc));
  }
  std::printf("\n");
  std::printf("effect %s, power %s, brightness %u, palette %ld, transition %lu ms%s\n",
              effect != nullptr ? effect->id : "-", s.on ? "on" : "off",
              static_cast<unsigned>(s.brightness), static_cast<long>(s.palette),
              static_cast<unsigned long>(s.transitionMs), s.testPattern ? ", test pattern" : "");
  for (uint8_t i = 0; effect != nullptr && i < s.paramCount; ++i) {
    std::printf("  %s = %ld (0x%lX)\n", effect->params[i].id, static_cast<long>(s.params[i]),
                static_cast<unsigned long>(s.params[i]));
  }
  std::printf("layout %ux%u, color order %u (%s)\n", static_cast<unsigned>(s.layout.width),
              static_cast<unsigned>(s.layout.height), static_cast<unsigned>(s.colorOrder),
              colorOrderName(s.colorOrder));
  std::printf("fps %u (target %u, shown %u), render avg %lu us max %lu us, show avg %lu us\n",
              static_cast<unsigned>(s.stats.frameFps), static_cast<unsigned>(s.fps),
              static_cast<unsigned>(s.stats.shownFps),
              static_cast<unsigned long>(s.stats.renderUsAvg),
              static_cast<unsigned long>(s.stats.renderUsMax),
              static_cast<unsigned long>(s.stats.showUsAvg));
  std::printf("state version %lu\n", static_cast<unsigned long>(s.version));

  const WifiStatus w = wifi.status();
  std::printf("wifi %s, network '%s', ip %s", wifiModeName(w.mode), w.ssid,
              w.ip[0] != '\0' ? w.ip : "-");
  if (w.mode == WifiMode::kConnected) {
    std::printf(", rssi %d dBm", static_cast<int>(w.rssi));
  } else if (w.mode == WifiMode::kAccessPoint) {
    std::printf(", ap '%s', %u clients%s", w.apName, static_cast<unsigned>(w.apClients),
                w.apRetrying ? ", retrying network" : "");
  }
  std::printf("\n");

  copyPresets(scratchPresets);
  std::printf("presets (%u of %u):", static_cast<unsigned>(scratchPresets.count()),
              static_cast<unsigned>(Presets::kMaxPresets));
  for (uint8_t i = 0; i < scratchPresets.count(); ++i) {
    const Preset& preset = *scratchPresets.at(i);
    const EffectDescriptor* presetEffect = EffectRegistry::global().at(preset.effect);
    std::printf(" %u=%s (%s)", static_cast<unsigned>(preset.id), preset.name,
                presetEffect != nullptr ? presetEffect->id : "?");
  }
  std::printf("\n");
}

#else

void App::pollConsole() {}
void App::runConsoleLine(const char* /*line*/) {}
void App::printInfo() const {}

#endif  // LEDFRAME_CONSOLE

}  // namespace ledframe

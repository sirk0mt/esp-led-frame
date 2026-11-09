#include "net/ApiJson.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "effects/EffectEngine.h"

namespace ledframe {

namespace {

constexpr int kBadRequest = 400;

const char* kindName(ParamKind kind) {
  switch (kind) {
    case ParamKind::Slider:
      return "slider";
    case ParamKind::Toggle:
      return "toggle";
    case ParamKind::Color:
      return "color";
    case ParamKind::Select:
      return "select";
    case ParamKind::Palette:
      return "palette";
  }
  return "?";
}

void hexColor(const CRGB& color, char (&out)[8]) {
  std::snprintf(out, sizeof(out), "#%02X%02X%02X", color.r, color.g, color.b);
}

// "#RRGGBB" -> 0xRRGGBB.
bool parseHexColor(const char* text, int32_t& out) {
  if (text == nullptr || std::strlen(text) != 7 || text[0] != '#') {
    return false;
  }
  int32_t value = 0;
  for (int i = 1; i < 7; ++i) {
    const char c = text[i];
    int digit = 0;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return false;
    }
    value = value * 16 + digit;
  }
  out = value;
  return true;
}

size_t finish(JsonDocument& doc, char* out, size_t capacity) {
  const size_t length = measureJson(doc);
  if (out == nullptr) {
    return capacity == 0 ? length : 0;  // Only a size query may omit the buffer.
  }
  if (length >= capacity) {
    return 0;
  }
  return serializeJson(doc, out, capacity);
}

bool parseBody(const char* body, size_t length, JsonDocument& doc, ApiError& error) {
  if (body == nullptr || deserializeJson(doc, body, length) != DeserializationError::Ok) {
    return error.set(kBadRequest, "invalid JSON");
  }
  if (!doc.is<JsonObjectConst>()) {
    return error.set(kBadRequest, "body must be a JSON object");
  }
  return true;
}

// Reads an optional integer field; false (with error set) when present but wrong.
bool readInt(JsonObjectConst object, const char* key, int32_t minimum, int32_t maximum,
             int32_t& out, bool& present, ApiError& error, const char* message) {
  const JsonVariantConst value = object[key];
  present = !value.isNull();
  if (!present) {
    return true;
  }
  if (!value.is<int32_t>()) {
    return error.set(kBadRequest, message);
  }
  const int32_t number = value.as<int32_t>();
  if (number < minimum || number > maximum) {
    return error.set(kBadRequest, message);
  }
  out = number;
  return true;
}

constexpr char kLedPinMessage[] = "ledPin must be 2, 4, 5, 13-19, 21-23, 25-27, 32 or 33";

template <uint8_t... Pins>
void writeLedPins(JsonArray out, std::integer_sequence<uint8_t, Pins...>) {
  (out.add(Pins), ...);
}

bool readBool(JsonObjectConst object, const char* key, bool& out, bool& present, ApiError& error,
              const char* message) {
  const JsonVariantConst value = object[key];
  present = !value.isNull();
  if (!present) {
    return true;
  }
  if (!value.is<bool>()) {
    return error.set(kBadRequest, message);
  }
  out = value.as<bool>();
  return true;
}

// Reads an optional string into out when it fits and passes valid (if given).
template <size_t N>
bool readText(JsonObjectConst object, const char* key, char (&out)[N], bool (*valid)(const char*),
              ApiError& error, const char* message) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) {
    return true;
  }
  if (!value.is<const char*>()) {
    return error.set(kBadRequest, message);
  }
  const char* text = value.as<const char*>();
  if (std::strlen(text) >= N || (valid != nullptr && !valid(text))) {
    return error.set(kBadRequest, message);
  }
  std::memcpy(out, text, std::strlen(text) + 1);
  return true;
}

void reportUnknown(JsonObjectConst object, const char* const* known, size_t knownCount,
                   IgnoredFields* ignored) {
  if (ignored == nullptr) {
    return;
  }
  for (JsonPairConst pair : object) {
    bool isKnown = false;
    for (size_t i = 0; i < knownCount; ++i) {
      if (std::strcmp(pair.key().c_str(), known[i]) == 0) {
        isKnown = true;
        break;
      }
    }
    if (!isKnown) {
      ignored->add(pair.key().c_str());
    }
  }
}

bool isWifiPassword(const char* text) {
  const size_t length = std::strlen(text);
  return length == 0 || (length >= 8 && length <= 64);
}

bool parseParamValue(JsonVariantConst value, const ParamSpec& spec, int32_t& out) {
  if (value.is<int32_t>()) {
    out = value.as<int32_t>();
    return true;
  }
  if (spec.kind == ParamKind::Toggle && value.is<bool>()) {
    out = value.as<bool>() ? 1 : 0;
    return true;
  }
  if (spec.kind == ParamKind::Color && value.is<const char*>()) {
    return parseHexColor(value.as<const char*>(), out);
  }
  return false;
}

void writeParamValues(JsonObject params, const EffectDescriptor& descriptor, const int32_t* values,
                      uint8_t count) {
  const uint8_t limit = count < descriptor.paramCount ? count : descriptor.paramCount;
  for (uint8_t i = 0; i < limit; ++i) {
    params[descriptor.params[i].id] = values[i];
  }
}

}  // namespace

void IgnoredFields::add(const char* name) {
  if (count < kMaxNames) {
    std::snprintf(names[count], sizeof(names[count]), "%s", name);
  }
  if (count < 255) {
    ++count;
  }
}

bool CommandBatch::add(const Command& command) {
  if (count >= kMaxCommands) {
    return false;
  }
  commands[count++] = command;
  return true;
}

// --- State ------------------------------------------------------------------

bool parseStatePatch(const char* body, size_t length, const EffectRegistry& registry,
                     uint8_t activeEffect, CommandBatch& out, ApiError& error,
                     IgnoredFields* ignored) {
  out.count = 0;
  error = ApiError{};
  JsonDocument doc;
  if (!parseBody(body, length, doc, error)) {
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  static const char* const kKnown[] = {"on", "bri", "transition", "effect", "params", "palette"};
  reportUnknown(root, kKnown, sizeof(kKnown) / sizeof(kKnown[0]), ignored);

  bool on = true;
  bool hasOn = false;
  int32_t brightness = 0;
  bool hasBrightness = false;
  int32_t transition = 0;
  bool hasTransition = false;
  if (!readBool(root, "on", on, hasOn, error, "on must be true or false") ||
      !readInt(root, "bri", 0, 255, brightness, hasBrightness, error,
               "bri must be an integer 0-255") ||
      !readInt(root, "transition", 0, static_cast<int32_t>(EffectEngine::kMaxTransitionMs),
               transition, hasTransition, error, "transition must be an integer 0-10000")) {
    return false;
  }

  // Effect: given by id, otherwise the active one is the target for "params".
  uint8_t effect = activeEffect;
  bool hasEffect = false;
  const JsonVariantConst effectValue = root["effect"];
  if (!effectValue.isNull()) {
    if (!effectValue.is<const char*>()) {
      return error.set(kBadRequest, "effect must be an effect id");
    }
    const int index = registry.indexOf(effectValue.as<const char*>());
    // Hidden effects (status) are shown by the device itself, never chosen by a user.
    if (index < 0 || (registry.at(static_cast<uint8_t>(index))->flags & kFlagHidden) != 0) {
      return error.set(kBadRequest, "unknown effect");
    }
    effect = static_cast<uint8_t>(index);
    hasEffect = true;
  }

  Command params = Command::setParams(effect);
  const JsonVariantConst paramsValue = root["params"];
  if (!paramsValue.isNull()) {
    if (!paramsValue.is<JsonObjectConst>()) {
      return error.set(kBadRequest, "params must be an object");
    }
    const EffectDescriptor* descriptor = registry.at(effect);
    if (descriptor == nullptr) {
      return error.set(kBadRequest, "no active effect for params");
    }
    for (JsonPairConst pair : paramsValue.as<JsonObjectConst>()) {
      const int index = findParam(*descriptor, pair.key().c_str());
      if (index < 0) {
        if (ignored != nullptr) {
          ignored->add(pair.key().c_str());
        }
        continue;
      }
      int32_t value = 0;
      if (!parseParamValue(pair.value(), descriptor->params[index], value)) {
        return error.set(kBadRequest, "parameter value must be an integer");
      }
      params.addParam(static_cast<uint8_t>(index), value);
    }
  }

  int32_t palette = 0;
  bool hasPalette = false;
  const JsonVariantConst paletteValue = root["palette"];
  if (!paletteValue.isNull()) {
    if (paletteValue.is<int32_t>()) {
      palette = paletteValue.as<int32_t>();
      if (palette < 0 || palette >= Palettes::kCount) {
        return error.set(kBadRequest, "unknown palette");
      }
    } else if (paletteValue.is<const char*>()) {
      palette = Palettes::indexOf(paletteValue.as<const char*>());
      if (palette < 0) {
        return error.set(kBadRequest, "unknown palette");
      }
    } else {
      return error.set(kBadRequest, "palette must be an index or an id");
    }
    hasPalette = true;
  }

  if (hasOn) {
    out.add(Command::setPower(on));
  }
  if (hasBrightness) {
    out.add(Command::setBrightness(static_cast<uint8_t>(brightness)));
  }
  if (hasTransition) {
    out.add(Command::setTransition(static_cast<uint32_t>(transition)));
  }
  if (params.paramCount > 0) {
    out.add(params);
  }
  if (hasEffect) {
    out.add(Command::setEffect(effect));
  }
  if (hasPalette) {
    out.add(Command::setPalette(palette));
  }
  return true;
}

size_t writeState(const StateSnapshot& state, const EffectRegistry& registry, char* out,
                  size_t capacity) {
  JsonDocument doc;
  doc["version"] = state.version;
  doc["on"] = state.on;
  doc["bri"] = state.brightness;
  const EffectDescriptor* effect = registry.at(state.effect);
  if (effect != nullptr) {
    doc["effect"] = effect->id;
  } else {
    doc["effect"] = nullptr;
  }
  doc["transition"] = state.transitionMs;
  doc["palette"] = state.palette;
  JsonObject params = doc["params"].to<JsonObject>();
  if (effect != nullptr) {
    writeParamValues(params, *effect, state.params, state.paramCount);
  }
  return finish(doc, out, capacity);
}

// --- Effects and palettes ---------------------------------------------------

size_t writeEffects(const EffectRegistry& registry, char* out, size_t capacity) {
  JsonDocument doc;
  JsonArray effects = doc.to<JsonArray>();
  for (const EffectDescriptor* descriptor : registry) {
    if ((descriptor->flags & kFlagHidden) != 0) {
      continue;
    }
    JsonObject item = effects.add<JsonObject>();
    item["id"] = descriptor->id;
    item["name"] = descriptor->name;
    item["order"] = descriptor->order;
    item["is2d"] = (descriptor->flags & kFlag2D) != 0;
    item["usesPalette"] = (descriptor->flags & kFlagPalette) != 0;
    item["static"] = (descriptor->flags & kFlagStatic) != 0;
    if (descriptor->widget != nullptr) {
      item["widget"] = descriptor->widget;
    } else {
      item["widget"] = nullptr;
    }
    JsonArray params = item["params"].to<JsonArray>();
    for (uint8_t i = 0; i < descriptor->paramCount; ++i) {
      const ParamSpec& spec = descriptor->params[i];
      JsonObject param = params.add<JsonObject>();
      param["id"] = spec.id;
      param["label"] = spec.label;
      param["kind"] = kindName(spec.kind);
      param["default"] = defaultParam(spec);
      switch (spec.kind) {
        case ParamKind::Slider:
          param["min"] = spec.min;
          param["max"] = spec.max;
          break;
        case ParamKind::Toggle:
          param["min"] = 0;
          param["max"] = 1;
          break;
        case ParamKind::Color:
          param["min"] = 0;
          param["max"] = 0xFFFFFF;
          break;
        case ParamKind::Select: {
          JsonArray options = param["options"].to<JsonArray>();
          // Split "a|b|c" into separate strings.
          const char* start = spec.options;
          for (const char* c = spec.options; c != nullptr; ++c) {
            if (*c == '|' || *c == '\0') {
              options.add(JsonString(start, static_cast<size_t>(c - start)));
              if (*c == '\0') {
                break;
              }
              start = c + 1;
            }
          }
          break;
        }
        case ParamKind::Palette:
          param["min"] = 0;
          param["max"] = Palettes::kCount - 1;
          break;
      }
    }
  }
  return finish(doc, out, capacity);
}

size_t writePalettes(const Palettes& palettes, uint8_t sampleCount, char* out, size_t capacity) {
  constexpr uint8_t kMaxSamples = 32;
  if (sampleCount > kMaxSamples) {
    sampleCount = kMaxSamples;
  }
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  CRGB samples[kMaxSamples];
  for (uint8_t i = 0; i < Palettes::kCount; ++i) {
    JsonObject item = list.add<JsonObject>();
    item["index"] = i;
    item["id"] = Palettes::id(i);
    item["name"] = Palettes::name(i);
    item["custom"] = i == Palettes::kUserIndex;
    JsonArray colors = item["colors"].to<JsonArray>();
    const uint8_t count = palettes.sample(i, samples, sampleCount);
    for (uint8_t s = 0; s < count; ++s) {
      char hex[8];
      hexColor(samples[s], hex);
      colors.add(hex);
    }
  }
  return finish(doc, out, capacity);
}

// --- Configuration ----------------------------------------------------------

size_t writeConfig(const Config& config, char* out, size_t capacity) {
  JsonDocument doc;
  JsonObject layout = doc["layout"].to<JsonObject>();
  layout["width"] = config.layout.width;
  layout["height"] = config.layout.height;
  layout["serpentine"] = config.layout.serpentine;
  layout["origin"] = originId(config.layout.origin);
  layout["rotation"] = static_cast<uint16_t>(config.layout.rotation);
  layout["flipX"] = config.layout.flipX;
  layout["flipY"] = config.layout.flipY;
  doc["colorOrder"] = config.colorOrder;
  doc["ledPin"] = config.ledPin;
  JsonObject power = doc["power"].to<JsonObject>();
  power["volts"] = config.powerVolts;
  power["milliamps"] = config.powerMilliamps;
  doc["fps"] = config.fps;
  doc["dither"] = config.dither;
  doc["gamma"] = config.gamma;
  doc["hostname"] = config.hostname;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = config.wifiSsid;
  wifi["hasPassword"] = config.wifiPassword[0] != '\0';
  doc["ota"]["hasPassword"] = config.otaPassword[0] != '\0';
  doc["timezone"] = config.timezone;
  doc["ntpServer"] = config.ntpServer;
  scheduleToJson(config.schedule, doc["schedule"].to<JsonArray>());
  return finish(doc, out, capacity);
}

bool parseConfigPatch(const char* body, size_t length, Config& config, ApiError& error,
                      IgnoredFields* ignored) {
  error = ApiError{};
  JsonDocument doc;
  if (!parseBody(body, length, doc, error)) {
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  static const char* const kKnown[] = {"layout",   "colorOrder", "ledPin",   "power",
                                       "fps",      "dither",     "gamma",    "hostname",
                                       "wifi",     "ota",        "timezone", "ntpServer",
                                       "schedule"};
  reportUnknown(root, kKnown, sizeof(kKnown) / sizeof(kKnown[0]), ignored);
  Config next = config;
  bool present = false;

  const JsonVariantConst layoutValue = root["layout"];
  if (!layoutValue.isNull()) {
    if (!layoutValue.is<JsonObjectConst>()) {
      return error.set(kBadRequest, "layout must be an object");
    }
    const JsonObjectConst layout = layoutValue.as<JsonObjectConst>();
    int32_t width = next.layout.width;
    int32_t height = next.layout.height;
    int32_t rotation = static_cast<int32_t>(next.layout.rotation);
    if (!readInt(layout, "width", 1, kMaxLeds, width, present, error, "invalid layout width") ||
        !readInt(layout, "height", 1, kMaxLeds, height, present, error, "invalid layout height") ||
        !readInt(layout, "rotation", 0, 270, rotation, present, error,
                 "rotation must be 0, 90, 180 or 270") ||
        !readBool(layout, "serpentine", next.layout.serpentine, present, error,
                  "serpentine must be true or false") ||
        !readBool(layout, "flipX", next.layout.flipX, present, error,
                  "flipX must be true or false") ||
        !readBool(layout, "flipY", next.layout.flipY, present, error,
                  "flipY must be true or false")) {
      return false;
    }
    const JsonVariantConst origin = layout["origin"];
    if (!origin.isNull() &&
        (!origin.is<const char*>() || !parseOrigin(origin.as<const char*>(), next.layout.origin))) {
      return error.set(kBadRequest, "unknown origin");
    }
    next.layout.width = static_cast<uint16_t>(width);
    next.layout.height = static_cast<uint16_t>(height);
    next.layout.rotation = static_cast<Rotation>(rotation);
    if (!Layout::isValid(next.layout)) {
      return error.set(kBadRequest, "invalid layout");
    }
  }

  int32_t number = next.colorOrder;
  if (!readInt(root, "colorOrder", 0, kColorOrderCount - 1, number, present, error,
               "colorOrder must be 0-5")) {
    return false;
  }
  next.colorOrder = static_cast<uint8_t>(number);

  number = next.ledPin;
  if (!readInt(root, "ledPin", 0, 255, number, present, error, kLedPinMessage) ||
      !isValidLedPin(static_cast<uint8_t>(number))) {
    return error.set(kBadRequest, kLedPinMessage);
  }
  next.ledPin = static_cast<uint8_t>(number);

  const JsonVariantConst powerValue = root["power"];
  if (!powerValue.isNull()) {
    if (!powerValue.is<JsonObjectConst>()) {
      return error.set(kBadRequest, "power must be an object");
    }
    const JsonObjectConst power = powerValue.as<JsonObjectConst>();
    int32_t volts = next.powerVolts;
    int32_t milliamps = static_cast<int32_t>(next.powerMilliamps);
    if (!readInt(power, "volts", 1, 24, volts, present, error, "volts must be 1-24") ||
        !readInt(power, "milliamps", 0, static_cast<int32_t>(Config::kMaxMilliamps), milliamps,
                 present, error, "milliamps must be 0-20000")) {
      return false;
    }
    next.powerVolts = static_cast<uint8_t>(volts);
    next.powerMilliamps = static_cast<uint32_t>(milliamps);
  }

  number = next.fps;
  if (!readInt(root, "fps", EffectEngine::kMinFps, EffectEngine::kMaxFps, number, present, error,
               "fps must be 10-120")) {
    return false;
  }
  next.fps = static_cast<uint8_t>(number);

  if (!readBool(root, "dither", next.dither, present, error, "dither must be true or false")) {
    return false;
  }
  if (!readBool(root, "gamma", next.gamma, present, error, "gamma must be true or false")) {
    return false;
  }

  if (!readText(root, "hostname", next.hostname, &isValidHostname, error,
                "hostname: 1-32 lowercase letters, digits and '-'")) {
    return false;
  }

  const JsonVariantConst wifiValue = root["wifi"];
  if (!wifiValue.isNull()) {
    if (!wifiValue.is<JsonObjectConst>()) {
      return error.set(kBadRequest, "wifi must be an object");
    }
    const JsonObjectConst wifi = wifiValue.as<JsonObjectConst>();
    if (!readText(wifi, "ssid", next.wifiSsid, nullptr, error, "ssid: up to 32 characters") ||
        !readText(wifi, "password", next.wifiPassword, &isWifiPassword, error,
                  "wifi password: empty or 8-64 characters")) {
      return false;
    }
    clearBlankSsid(next.wifiSsid);
  }

  const JsonVariantConst otaValue = root["ota"];
  if (!otaValue.isNull()) {
    if (!otaValue.is<JsonObjectConst>()) {
      return error.set(kBadRequest, "ota must be an object");
    }
    if (!readText(otaValue.as<JsonObjectConst>(), "password", next.otaPassword, nullptr, error,
                  "ota password: up to 64 characters")) {
      return false;
    }
  }

  if (!readText(root, "timezone", next.timezone, &isValidTimezone, error,
                "timezone: a POSIX TZ rule, 1-63 characters, e.g. CET-1CEST,M3.5.0,M10.5.0/3") ||
      !readText(root, "ntpServer", next.ntpServer, &isValidNtpServer, error,
                "ntpServer: 1-64 letters, digits, '.' and '-'")) {
    return false;
  }

  if (!root["schedule"].isNull()) {
    const char* message = "";
    if (!scheduleFromJson(root["schedule"], next.schedule, message)) {
      return error.set(kBadRequest, message);
    }
  }

  config = next;
  return true;
}

// --- Presets ----------------------------------------------------------------

size_t writePresets(const Presets& presets, const EffectRegistry& registry, char* out,
                    size_t capacity) {
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (uint8_t i = 0; i < presets.count(); ++i) {
    const Preset& preset = *presets.at(i);
    const EffectDescriptor* effect = registry.at(preset.effect);
    if (effect == nullptr) {
      continue;
    }
    JsonObject item = list.add<JsonObject>();
    item["id"] = preset.id;
    item["name"] = preset.name;
    item["effect"] = effect->id;
    item["bri"] = preset.brightness;
    item["palette"] = preset.palette;
    writeParamValues(item["params"].to<JsonObject>(), *effect, preset.params, preset.paramCount);
  }
  return finish(doc, out, capacity);
}

bool parsePresetName(const char* body, size_t length, char (&name)[Preset::kNameMax + 1],
                     ApiError& error) {
  error = ApiError{};
  JsonDocument doc;
  if (!parseBody(body, length, doc, error)) {
    return false;
  }
  const JsonVariantConst value = doc["name"];
  if (!value.is<const char*>() || !Presets::isValidName(value.as<const char*>())) {
    return error.set(kBadRequest, "name: 1-32 characters");
  }
  std::snprintf(name, sizeof(name), "%s", value.as<const char*>());
  return true;
}

PresetRoute parsePresetPath(const char* path, uint8_t& id) {
  static const char kPrefix[] = "/api/presets/";
  const size_t prefixLength = sizeof(kPrefix) - 1;
  if (path == nullptr || std::strncmp(path, kPrefix, prefixLength) != 0) {
    return PresetRoute::kInvalid;
  }
  const char* c = path + prefixLength;
  unsigned value = 0;
  int digits = 0;
  while (*c >= '0' && *c <= '9' && digits < 4) {
    value = value * 10 + static_cast<unsigned>(*c - '0');
    ++c;
    ++digits;
  }
  if (digits == 0 || value == 0 || value > 255) {
    return PresetRoute::kInvalid;
  }
  id = static_cast<uint8_t>(value);
  if (*c == '\0') {
    return PresetRoute::kItem;
  }
  if (std::strcmp(c, "/apply") == 0) {
    return PresetRoute::kApply;
  }
  return PresetRoute::kInvalid;
}

// --- System -----------------------------------------------------------------

size_t writeInfo(const SystemInfo& info, char* out, size_t capacity) {
  JsonDocument doc;
  doc["version"] = info.version;
  doc["variant"] = info.variant;
  doc["repo"] = info.repo;
  doc["buildDate"] = info.buildDate;
  doc["uptime"] = info.uptimeS;
  doc["freeHeap"] = info.freeHeap;
  doc["minFreeHeap"] = info.minFreeHeap;
  doc["leds"] = info.ledCount;
  doc["maxLeds"] = info.maxLeds;
  doc["testPattern"] = info.testPattern;
  doc["ledPin"] = info.ledPin;
  writeLedPins(doc["ledPins"].to<JsonArray>(), LedPins{});
  JsonObject engine = doc["engine"].to<JsonObject>();
  engine["targetFps"] = info.fps;
  engine["fps"] = info.stats.frameFps;
  engine["shownFps"] = info.stats.shownFps;
  engine["renderUsAvg"] = info.stats.renderUsAvg;
  engine["renderUsMax"] = info.stats.renderUsMax;
  engine["showUsAvg"] = info.stats.showUsAvg;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = wifiModeName(info.wifi.mode);
  wifi["ssid"] = info.wifi.ssid;
  wifi["ip"] = info.wifi.ip;
  if (info.wifi.mode == WifiMode::kConnected) {
    wifi["rssi"] = info.wifi.rssi;
  }
  if (info.wifi.mode == WifiMode::kAccessPoint) {
    wifi["apName"] = info.wifi.apName;
    wifi["apClients"] = info.wifi.apClients;
    wifi["retrying"] = info.wifi.apRetrying;  // trying the saved network right now
  }
  JsonObject time = doc["time"].to<JsonObject>();
  time["synced"] = info.time.known;
  time["sntp"] = info.sntp;
  if (info.time.known) {
    char local[32];  // 19 characters; the compiler cannot tell the fields fit
    std::snprintf(local, sizeof(local), "%04u-%02u-%02u %02u:%02u:%02u",
                  static_cast<unsigned>(info.time.year), static_cast<unsigned>(info.time.month),
                  static_cast<unsigned>(info.time.day), static_cast<unsigned>(info.time.hour),
                  static_cast<unsigned>(info.time.minute), static_cast<unsigned>(info.time.second));
    time["local"] = local;
    time["weekday"] = info.time.weekday;
  }
  if (info.crash != nullptr) {
    doc["resetReason"] = info.crash->resetReason;
    if (info.crash->hasCoredump) {
      JsonObject crash = doc["lastCrash"].to<JsonObject>();
      crash["task"] = info.crash->task;
      char hex[11];
      std::snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(info.crash->pc));
      crash["pc"] = hex;
      JsonArray backtrace = crash["backtrace"].to<JsonArray>();
      for (uint8_t i = 0; i < info.crash->depth && i < CrashInfo::kMaxBacktrace; ++i) {
        std::snprintf(hex, sizeof(hex), "0x%08lX",
                      static_cast<unsigned long>(info.crash->backtrace[i]));
        backtrace.add(hex);
      }
      crash["backtraceCorrupted"] = info.crash->backtraceCorrupted;
    } else {
      doc["lastCrash"] = nullptr;
    }
  }
  return finish(doc, out, capacity);
}

bool isDeviceHost(const char* host, const char* apAddress, const char* hostname) {
  if (host == nullptr || apAddress == nullptr || hostname == nullptr) {
    return false;
  }
  const char* colon = std::strchr(host, ':');
  const size_t length = colon != nullptr ? static_cast<size_t>(colon - host) : std::strlen(host);
  if (length == std::strlen(apAddress) && std::strncmp(host, apAddress, length) == 0) {
    return true;
  }
  static const char kLocal[] = ".local";
  const size_t nameLength = std::strlen(hostname);
  if (nameLength == 0 || length != nameLength + sizeof(kLocal) - 1) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char expected = i < nameLength ? hostname[i] : kLocal[i - nameLength];
    if (std::tolower(static_cast<unsigned char>(host[i])) !=
        std::tolower(static_cast<unsigned char>(expected))) {
      return false;
    }
  }
  return true;
}

bool isSameOrigin(const char* origin, const char* host) {
  if (origin == nullptr || origin[0] == '\0') {
    return true;
  }
  if (host == nullptr || host[0] == '\0') {
    return false;
  }
  static const char* const kSchemes[] = {"http://", "https://"};
  for (const char* scheme : kSchemes) {
    const size_t schemeLength = std::strlen(scheme);
    if (std::strlen(origin) != schemeLength + std::strlen(host)) {
      continue;
    }
    bool same = true;
    for (size_t i = 0; origin[i] != '\0' && same; ++i) {
      const char expected = i < schemeLength ? scheme[i] : host[i - schemeLength];
      same = std::tolower(static_cast<unsigned char>(origin[i])) ==
             std::tolower(static_cast<unsigned char>(expected));
    }
    if (same) {
      return true;
    }
  }
  return false;
}

namespace {

// Domains that only exist in local networks: special-use names (RFC 6762, RFC 8375),
// the name ICANN keeps for private use (.internal), and names home routers use that
// are not top-level domains on the internet (fritz.box, .lan, .home, ...).
constexpr const char* kLocalDomains[] = {"local", "lan", "home", "home.arpa", "internal",
                                         "intranet", "localdomain", "corp", "private", "box"};

bool equalsIgnoreCase(const char* a, const char* b, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Four decimal numbers 0..255 separated by dots.
bool isIpv4(const char* text, size_t length) {
  int parts = 0;
  size_t i = 0;
  while (i < length) {
    size_t digits = 0;
    int value = 0;
    while (i < length && text[i] >= '0' && text[i] <= '9' && digits < 4) {
      value = value * 10 + (text[i] - '0');
      ++digits;
      ++i;
    }
    if (digits == 0 || digits > 3 || value > 255) {
      return false;
    }
    ++parts;
    if (i == length) {
      break;
    }
    if (text[i] != '.' || parts == 4) {
      return false;
    }
    ++i;
    if (i == length) {
      return false;  // trailing dot
    }
  }
  return parts == 4;
}

}  // namespace

bool isDeviceName(const char* host, const char* hostname) {
  if (host == nullptr || hostname == nullptr) {
    return false;
  }
  size_t length = std::strlen(host);
  if (length > 2 && host[0] == '[') {
    // IPv6 literal, with or without a port: "[fe80::1]" or "[fe80::1]:80".
    const char* end = std::strchr(host, ']');
    return end != nullptr && (end[1] == '\0' || end[1] == ':');
  }
  // Port: the part after the last colon, digits only.
  const char* colon = std::strrchr(host, ':');
  if (colon != nullptr) {
    for (const char* c = colon + 1; *c != '\0'; ++c) {
      if (*c < '0' || *c > '9') {
        return false;
      }
    }
    length = static_cast<size_t>(colon - host);
  }
  if (length > 0 && host[length - 1] == '.') {
    --length;  // "ledframe.local." is the same name
  }
  if (length == 0) {
    return false;
  }
  if (isIpv4(host, length)) {
    return true;
  }
  if (length == 9 && equalsIgnoreCase(host, "localhost", 9)) {
    return true;
  }
  const size_t nameLength = std::strlen(hostname);
  if (nameLength == 0 || length < nameLength || !equalsIgnoreCase(host, hostname, nameLength)) {
    return false;
  }
  if (length == nameLength) {
    return true;
  }
  if (host[nameLength] != '.') {
    return false;
  }
  // hostname + "." + something that ends in a local domain: "ledframe.lan" or
  // "ledframe.fritz.box", where the domain is the whole rest or its last labels.
  const char* rest = host + nameLength + 1;
  const size_t restLength = length - nameLength - 1;
  for (const char* domain : kLocalDomains) {
    const size_t domainLength = std::strlen(domain);
    if (restLength == domainLength && equalsIgnoreCase(rest, domain, domainLength)) {
      return true;
    }
    if (restLength > domainLength && rest[restLength - domainLength - 1] == '.' &&
        equalsIgnoreCase(rest + restLength - domainLength, domain, domainLength)) {
      return true;
    }
  }
  return false;
}

const char* refuseRequest(const char* origin, const char* host, const char* hostname) {
  const bool hasHost = host != nullptr && host[0] != '\0';
  if (hasHost && !isDeviceName(host, hostname)) {
    return "host name is not this device";
  }
  if (!isSameOrigin(origin, host)) {
    return "cross-origin request refused";
  }
  return nullptr;
}

bool parseTestPattern(const char* body, size_t length, bool& on, ApiError& error) {
  error = ApiError{};
  JsonDocument doc;
  if (!parseBody(body, length, doc, error)) {
    return false;
  }
  const JsonVariantConst value = doc["on"];
  if (!value.is<bool>()) {
    return error.set(kBadRequest, "on must be true or false");
  }
  on = value.as<bool>();
  return true;
}

size_t writeScan(const WifiNetwork* networks, uint8_t count, bool scanning, char* out,
                 size_t capacity) {
  JsonDocument doc;
  doc["scanning"] = scanning;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (uint8_t i = 0; networks != nullptr && i < count; ++i) {
    JsonObject item = list.add<JsonObject>();
    item["ssid"] = networks[i].ssid;
    item["rssi"] = networks[i].rssi;
    item["secure"] = networks[i].secure;
  }
  return finish(doc, out, capacity);
}

size_t writeError(const char* message, char* out, size_t capacity) {
  JsonDocument doc;
  doc["error"] = message;
  return finish(doc, out, capacity);
}

}  // namespace ledframe

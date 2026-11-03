#include "state/Config.h"

#include <ArduinoJson.h>

#include <cstring>

namespace ledframe {

namespace {

struct OriginName {
  Origin origin;
  const char* id;
};

constexpr OriginName kOrigins[] = {
    {Origin::kTopLeft, "top-left"},
    {Origin::kTopRight, "top-right"},
    {Origin::kBottomLeft, "bottom-left"},
    {Origin::kBottomRight, "bottom-right"},
};

// Copies a string field when it fits, otherwise keeps the current value.
template <size_t N>
void readString(JsonVariantConst value, char (&out)[N], bool (*valid)(const char*) = nullptr) {
  if (!value.is<const char*>()) {
    return;
  }
  const char* text = value.as<const char*>();
  const size_t length = std::strlen(text);
  if (length >= N || (valid != nullptr && !valid(text))) {
    return;
  }
  std::memcpy(out, text, length + 1);
}

template <typename T>
void readNumber(JsonVariantConst value, T minimum, T maximum, T& out) {
  if (!value.is<T>()) {
    return;
  }
  const T number = value.as<T>();
  if (number >= minimum && number <= maximum) {
    out = number;
  }
}

void readBool(JsonVariantConst value, bool& out) {
  if (value.is<bool>()) {
    out = value.as<bool>();
  }
}

void readLayout(JsonObjectConst json, LayoutConfig& out) {
  LayoutConfig layout;  // Starts from defaults; used only when the whole layout is valid.
  readNumber<uint16_t>(json["width"], 1, kMaxLeds, layout.width);
  readNumber<uint16_t>(json["height"], 1, kMaxLeds, layout.height);
  readBool(json["serpentine"], layout.serpentine);
  if (json["origin"].is<const char*>()) {
    parseOrigin(json["origin"].as<const char*>(), layout.origin);
  }
  uint16_t rotation = static_cast<uint16_t>(layout.rotation);
  readNumber<uint16_t>(json["rotation"], 0, 270, rotation);
  layout.rotation = static_cast<Rotation>(rotation);
  readBool(json["flipX"], layout.flipX);
  readBool(json["flipY"], layout.flipY);
  if (Layout::isValid(layout)) {
    out = layout;
  }
}

}  // namespace

bool operator==(const Config& a, const Config& b) {
  return a.layout.width == b.layout.width && a.layout.height == b.layout.height &&
         a.layout.serpentine == b.layout.serpentine && a.layout.origin == b.layout.origin &&
         a.layout.rotation == b.layout.rotation && a.layout.flipX == b.layout.flipX &&
         a.layout.flipY == b.layout.flipY && a.colorOrder == b.colorOrder && a.ledPin == b.ledPin &&
         a.powerVolts == b.powerVolts && a.powerMilliamps == b.powerMilliamps && a.fps == b.fps &&
         a.dither == b.dither && a.gamma == b.gamma && std::strcmp(a.hostname, b.hostname) == 0 &&
         std::strcmp(a.wifiSsid, b.wifiSsid) == 0 &&
         std::strcmp(a.wifiPassword, b.wifiPassword) == 0 &&
         std::strcmp(a.otaPassword, b.otaPassword) == 0 &&
         std::strcmp(a.timezone, b.timezone) == 0 && std::strcmp(a.ntpServer, b.ntpServer) == 0 &&
         a.schedule == b.schedule;
}

const char* originId(Origin origin) {
  for (const OriginName& entry : kOrigins) {
    if (entry.origin == origin) {
      return entry.id;
    }
  }
  return "top-left";
}

bool parseOrigin(const char* id, Origin& out) {
  if (id == nullptr) {
    return false;
  }
  for (const OriginName& entry : kOrigins) {
    if (std::strcmp(entry.id, id) == 0) {
      out = entry.origin;
      return true;
    }
  }
  return false;
}

bool isValidHostname(const char* hostname) {
  if (hostname == nullptr) {
    return false;
  }
  const size_t length = std::strlen(hostname);
  if (length == 0 || length > Config::kHostnameMax || hostname[0] == '-' ||
      hostname[length - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = hostname[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return false;
    }
  }
  return true;
}

bool isValidTimezone(const char* timezone) {
  if (timezone == nullptr) {
    return false;
  }
  const size_t length = std::strlen(timezone);
  if (length == 0 || length > Config::kTimezoneMax) {
    return false;
  }
  const char first = timezone[0];
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '<')) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = timezone[i];
    const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    const bool digit = c >= '0' && c <= '9';
    if (!letter && !digit && std::strchr("+-,./:<>", c) == nullptr) {
      return false;
    }
  }
  return true;
}

bool isValidNtpServer(const char* server) {
  if (server == nullptr) {
    return false;
  }
  const size_t length = std::strlen(server);
  if (length == 0 || length > Config::kNtpServerMax) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = server[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
          c == '-')) {
      return false;
    }
  }
  return true;
}

size_t serializeConfig(const Config& config, IByteWriter& out) {
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
  wifi["password"] = config.wifiPassword;
  doc["ota"]["password"] = config.otaPassword;
  doc["timezone"] = config.timezone;
  doc["ntpServer"] = config.ntpServer;
  scheduleToJson(config.schedule, doc["schedule"].to<JsonArray>());
  return serializeJson(doc, out);
}

size_t serializeConfig(const Config& config, char* out, size_t capacity) {
  return writeToArray(out, capacity,
                      [&](IByteWriter& writer) { return serializeConfig(config, writer); });
}

bool deserializeConfig(const char* json, size_t length, Config& out) {
  ArrayReader reader(json, length);
  return deserializeConfig(reader, out);
}

bool deserializeConfig(IByteReader& in, Config& out) {
  out = Config{};
  JsonDocument doc;
  if (deserializeJson(doc, in) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root["layout"].is<JsonObjectConst>()) {
    readLayout(root["layout"].as<JsonObjectConst>(), out.layout);
  }
  readNumber<uint8_t>(root["colorOrder"], 0, kColorOrderCount - 1, out.colorOrder);
  uint8_t ledPin = out.ledPin;
  readNumber<uint8_t>(root["ledPin"], 0, 255, ledPin);
  if (isValidLedPin(ledPin)) {
    out.ledPin = ledPin;
  }
  readNumber<uint8_t>(root["power"]["volts"], 1, 24, out.powerVolts);
  readNumber<uint32_t>(root["power"]["milliamps"], 0, Config::kMaxMilliamps, out.powerMilliamps);
  readNumber<uint8_t>(root["fps"], 10, 120, out.fps);
  readBool(root["dither"], out.dither);
  readBool(root["gamma"], out.gamma);
  readString(root["hostname"], out.hostname, &isValidHostname);
  readString(root["wifi"]["ssid"], out.wifiSsid);
  clearBlankSsid(out.wifiSsid);
  readString(root["wifi"]["password"], out.wifiPassword);
  readString(root["ota"]["password"], out.otaPassword);
  readString(root["timezone"], out.timezone, &isValidTimezone);
  readString(root["ntpServer"], out.ntpServer, &isValidNtpServer);
  const char* error = nullptr;
  if (!root["schedule"].isNull()) {
    scheduleFromJson(root["schedule"], out.schedule, error);  // invalid: stays empty
  }
  return true;
}

void clearBlankSsid(char* ssid) {
  if (ssid == nullptr) {
    return;
  }
  for (const char* c = ssid; *c != '\0'; ++c) {
    if (*c != ' ' && *c != '\t') {
      return;
    }
  }
  ssid[0] = '\0';
}

}  // namespace ledframe

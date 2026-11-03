#include "state/State.h"

#include <ArduinoJson.h>

#include "effects/Command.h"
#include "effects/EffectEngine.h"

namespace ledframe {

const char kDefaultEffectId[] = "solid";

namespace {

// Clamps like the engine does, including the palette list bound.
int32_t clampValue(const ParamSpec& spec, int32_t value) {
  const int32_t clamped = clampParam(spec, value);
  return spec.kind == ParamKind::Palette ? Palettes::clampIndex(clamped) : clamped;
}

CRGB toColor(uint32_t rgb) {
  return CRGB(rgb);
}

uint32_t fromColor(const CRGB& color) {
  return (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

void readParams(JsonObjectConst json, const EffectRegistry& registry, State& out) {
  for (JsonPairConst effectEntry : json) {
    const int effect = registry.indexOf(effectEntry.key().c_str());
    if (effect < 0 || !effectEntry.value().is<JsonObjectConst>()) {
      continue;  // Unknown effect: skipped.
    }
    const EffectDescriptor& descriptor = *registry.at(static_cast<uint8_t>(effect));
    for (JsonPairConst paramEntry : effectEntry.value().as<JsonObjectConst>()) {
      const int param = findParam(descriptor, paramEntry.key().c_str());
      if (param < 0 || !paramEntry.value().is<int32_t>()) {
        continue;  // Unknown parameter or not an integer: skipped.
      }
      out.params[effect][param] =
          clampValue(descriptor.params[param], paramEntry.value().as<int32_t>());
    }
  }
}

}  // namespace

void State::reset(const EffectRegistry& registry) {
  *this = State{};
  for (uint8_t i = 0; i < registry.size(); ++i) {
    loadDefaults(*registry.at(i), params[i]);
  }
  const int solid = registry.indexOf(kDefaultEffectId);
  if (solid >= 0) {
    effect = static_cast<uint8_t>(solid);
  } else if (registry.size() > 0) {
    effect = 0;
  }
}

size_t serializeState(const State& state, const EffectRegistry& registry, IByteWriter& out) {
  JsonDocument doc;
  doc["on"] = state.on;
  doc["bri"] = state.brightness;
  const EffectDescriptor* active = registry.at(state.effect);
  if (active != nullptr) {
    doc["effect"] = active->id;
  }
  doc["transition"] = state.transitionMs;
  doc["palette"] = state.palette;
  JsonArray colors = doc["userColors"].to<JsonArray>();
  for (uint32_t color : state.userColors) {
    colors.add(color);
  }
  JsonObject params = doc["params"].to<JsonObject>();
  for (uint8_t i = 0; i < registry.size(); ++i) {
    const EffectDescriptor& descriptor = *registry.at(i);
    if (descriptor.paramCount == 0) {
      continue;
    }
    JsonObject values = params[descriptor.id].to<JsonObject>();
    for (uint8_t p = 0; p < descriptor.paramCount; ++p) {
      values[descriptor.params[p].id] = state.params[i][p];
    }
  }
  return serializeJson(doc, out);
}

size_t serializeState(const State& state, const EffectRegistry& registry, char* out,
                      size_t capacity) {
  return writeToArray(out, capacity,
                      [&](IByteWriter& writer) { return serializeState(state, registry, writer); });
}

bool deserializeState(const char* json, size_t length, const EffectRegistry& registry, State& out) {
  ArrayReader reader(json, length);
  return deserializeState(reader, registry, out);
}

bool deserializeState(IByteReader& in, const EffectRegistry& registry, State& out) {
  out.reset(registry);
  JsonDocument doc;
  if (deserializeJson(doc, in) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root["on"].is<bool>()) {
    out.on = root["on"].as<bool>();
  }
  if (root["bri"].is<uint8_t>()) {
    out.brightness = root["bri"].as<uint8_t>();
  }
  if (root["effect"].is<const char*>()) {
    const int effect = registry.indexOf(root["effect"].as<const char*>());
    if (effect >= 0) {
      out.effect = static_cast<uint8_t>(effect);  // Unknown effect: default stays.
    }
  }
  if (root["transition"].is<uint32_t>()) {
    const uint32_t ms = root["transition"].as<uint32_t>();
    out.transitionMs = ms > EffectEngine::kMaxTransitionMs ? EffectEngine::kMaxTransitionMs : ms;
  }
  if (root["palette"].is<int32_t>()) {
    out.palette = Palettes::clampIndex(root["palette"].as<int32_t>());
  }
  if (root["userColors"].is<JsonArrayConst>()) {
    const JsonArrayConst colors = root["userColors"].as<JsonArrayConst>();
    if (colors.size() == Palettes::kUserColorCount) {
      for (uint8_t i = 0; i < Palettes::kUserColorCount; ++i) {
        if (colors[i].is<uint32_t>()) {
          out.userColors[i] = colors[i].as<uint32_t>() & 0xFFFFFFu;
        }
      }
    }
  }
  if (root["params"].is<JsonObjectConst>()) {
    readParams(root["params"].as<JsonObjectConst>(), registry, out);
  }
  return true;
}

void captureState(const EffectEngine& engine, const EffectRegistry& registry,
                  const Palettes& palettes, State& out) {
  out.on = engine.on();
  out.brightness = engine.brightness();
  if (engine.activeEffect() < registry.size()) {
    out.effect = engine.activeEffect();
  }
  out.transitionMs = engine.transitionMs();
  out.palette = engine.paletteIndex();
  for (uint8_t i = 0; i < Palettes::kUserColorCount; ++i) {
    out.userColors[i] = fromColor(palettes.userColor(i));
  }
  for (uint8_t i = 0; i < registry.size(); ++i) {
    const int32_t* values = engine.params(i);
    const uint8_t count = registry.at(i)->paramCount;
    for (uint8_t p = 0; p < count; ++p) {
      out.params[i][p] = values[p];
    }
  }
}

void applyState(const State& state, const EffectRegistry& registry, Palettes& palettes,
                EffectEngine& engine) {
  palettes.setUserColors(toColor(state.userColors[0]), toColor(state.userColors[1]),
                         toColor(state.userColors[2]));
  for (uint8_t i = 0; i < registry.size(); ++i) {
    const EffectDescriptor& descriptor = *registry.at(i);
    Command command = Command::setParams(i);
    for (uint8_t p = 0; p < descriptor.paramCount; ++p) {
      command.addParam(p, state.params[i][p]);
    }
    engine.apply(command);
  }
  engine.apply(Command::setTransition(state.transitionMs));
  engine.apply(Command::setBrightness(state.brightness));
  engine.apply(Command::setPower(state.on));
  if (state.effect < registry.size()) {
    engine.apply(Command::setEffect(state.effect));
  }
  engine.apply(Command::setPalette(state.palette));
}

}  // namespace ledframe

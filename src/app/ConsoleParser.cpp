#include "app/ConsoleParser.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "state/Config.h"

namespace ledframe {

const char kConsoleHelp[] =
    "commands: effect <id> | bri <0-255> | param <id> <value> | order <0-5> | test | info | "
    "preset save <name> | preset apply <id> | wifi <ssid> [password]\n"
    "quote words with spaces: wifi \"My Network\" \"pass word\"";

namespace {

constexpr uint8_t kMaxTokens = 4;

struct Tokens {
  char buffer[kConsoleLineMax + 1];
  const char* items[kMaxTokens];
  uint8_t count = 0;
  bool tooMany = false;
};

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Splits a copy of the line on whitespace. A token in double quotes may contain
// spaces and apostrophes; inside quotes \" is a quote and \\ a backslash. Tokens are
// unescaped in place: the write position never passes the read position, because
// quotes and escapes only shorten the text. Returns an error message or nullptr.
const char* tokenize(const char* line, Tokens& tokens) {
  const size_t length = std::strlen(line);
  if (length > kConsoleLineMax) {
    return "line too long";
  }
  std::memcpy(tokens.buffer, line, length + 1);
  const char* read = tokens.buffer;
  char* write = tokens.buffer;
  for (;;) {
    while (isSpace(*read)) {
      ++read;
    }
    if (*read == '\0') {
      return nullptr;
    }
    if (tokens.count == kMaxTokens) {
      tokens.tooMany = true;
      return nullptr;
    }
    char* start = write;
    if (*read == '"') {
      ++read;
      for (;;) {
        if (*read == '\0') {
          return "missing closing quote";
        }
        if (*read == '\\' && (read[1] == '"' || read[1] == '\\')) {
          *write++ = read[1];
          read += 2;
        } else if (*read == '"') {
          ++read;
          break;
        } else {
          *write++ = *read++;
        }
      }
      if (*read != '\0' && !isSpace(*read)) {
        return "space expected after closing quote";
      }
    } else {
      while (*read != '\0' && !isSpace(*read)) {
        *write++ = *read++;
      }
    }
    // read points at a separator or at the end. When nothing was removed, write
    // equals read, so the terminator replaces the separator.
    const bool atEnd = *read == '\0';
    *write++ = '\0';
    tokens.items[tokens.count++] = start;
    if (atEnd) {
      return nullptr;
    }
    ++read;
  }
}

// Parses a whole token as a decimal or 0x-prefixed hexadecimal integer.
bool parseInt(const char* text, long minimum, long maximum, int32_t& out) {
  errno = 0;
  char* end = nullptr;
  const long value = std::strtol(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
    return false;
  }
  out = static_cast<int32_t>(value);
  return true;
}

ConsoleResult error(const char* message) {
  ConsoleResult result;
  result.action = ConsoleAction::kError;
  result.error = message;
  return result;
}

bool is(const char* token, const char* word) {
  return std::strcmp(token, word) == 0;
}

}  // namespace

ConsoleResult parseConsoleLine(const char* line, const EffectRegistry& registry,
                               uint8_t activeEffect) {
  Tokens tokens;
  if (line == nullptr) {
    return error("line too long");
  }
  if (const char* problem = tokenize(line, tokens)) {
    return error(problem);
  }
  if (tokens.count == 0) {
    return ConsoleResult{};
  }
  const char* name = tokens.items[0];
  const uint8_t args = static_cast<uint8_t>(tokens.count - 1);
  ConsoleResult result;

  if (is(name, "effect")) {
    if (args != 1 || tokens.tooMany) {
      return error("usage: effect <id>");
    }
    const int index = registry.indexOf(tokens.items[1]);
    if (index < 0) {
      return error("unknown effect");
    }
    result.action = ConsoleAction::kEngine;
    result.command = Command::setEffect(static_cast<uint8_t>(index));
    return result;
  }

  if (is(name, "bri")) {
    int32_t value = 0;
    if (args != 1 || tokens.tooMany || !parseInt(tokens.items[1], 0, 255, value)) {
      return error("usage: bri <0-255>");
    }
    result.action = ConsoleAction::kEngine;
    result.command = Command::setBrightness(static_cast<uint8_t>(value));
    return result;
  }

  if (is(name, "param")) {
    int32_t value = 0;
    if (args != 2 || tokens.tooMany || !parseInt(tokens.items[2], INT32_MIN, INT32_MAX, value)) {
      return error("usage: param <id> <value>");
    }
    const EffectDescriptor* effect = registry.at(activeEffect);
    if (effect == nullptr) {
      return error("no active effect");
    }
    const int param = findParam(*effect, tokens.items[1]);
    if (param < 0) {
      return error("unknown parameter");
    }
    result.action = ConsoleAction::kEngine;
    result.command = Command::setParams(activeEffect);
    result.command.addParam(static_cast<uint8_t>(param), value);
    return result;
  }

  if (is(name, "order")) {
    int32_t value = 0;
    if (args != 1 || tokens.tooMany || !parseInt(tokens.items[1], 0, 5, value)) {
      return error("usage: order <0-5>");
    }
    result.action = ConsoleAction::kColorOrder;
    result.colorOrder = static_cast<uint8_t>(value);
    return result;
  }

  if (is(name, "test") || is(name, "info")) {
    if (args != 0) {
      return error(is(name, "test") ? "usage: test" : "usage: info");
    }
    result.action = is(name, "test") ? ConsoleAction::kTestPattern : ConsoleAction::kInfo;
    return result;
  }

  if (is(name, "preset")) {
    if (args == 2 && !tokens.tooMany && is(tokens.items[1], "save")) {
      if (!Presets::isValidName(tokens.items[2])) {
        return error("invalid preset name");
      }
      result.action = ConsoleAction::kPresetSave;
      std::strcpy(result.presetName, tokens.items[2]);  // isValidName() bounds the length.
      return result;
    }
    int32_t id = 0;
    if (args == 2 && !tokens.tooMany && is(tokens.items[1], "apply") &&
        parseInt(tokens.items[2], 1, 255, id)) {
      result.action = ConsoleAction::kPresetApply;
      result.presetId = static_cast<uint8_t>(id);
      return result;
    }
    return error("usage: preset save <name> | preset apply <id>");
  }

  if (is(name, "wifi")) {
    if ((args != 1 && args != 2) || tokens.tooMany) {
      return error("usage: wifi <ssid> [password]");
    }
    const char* ssid = tokens.items[1];
    const char* password = args == 2 ? tokens.items[2] : "";
    const size_t passwordLength = std::strlen(password);
    if (std::strlen(ssid) > 32) {
      return error("ssid longer than 32 characters");
    }
    // WPA2: 8 to 63 characters, or a 64-character hex key; empty for an open network.
    if (passwordLength != 0 && (passwordLength < 8 || passwordLength > 64)) {
      return error("password must have 8 to 64 characters");
    }
    result.action = ConsoleAction::kWifi;
    std::strcpy(result.wifiSsid, ssid);
    clearBlankSsid(result.wifiSsid);  // wifi " " means no network, like wifi ""
    std::strcpy(result.wifiPassword, password);
    return result;
  }

  return error("unknown command");
}

}  // namespace ledframe

// Parser for serial console lines (esp32dev only at run time, tested natively).
//
// Commands:
//   effect <id>          switch effect
//   bri <0-255>          global brightness
//   param <id> <value>   parameter of the active effect; value may be hex (0xFF0000)
//   order <0-5>          LED color order (RGB, RBG, GRB, GBR, BRG, BGR)
//   test                 test pattern until the next command other than "order"
//   info                 version, state and engine statistics
//   preset save <name>   save the active effect, palette and brightness as a preset
//                        (an existing name is overwritten)
//   preset apply <id>    apply a preset with the normal transition
//   wifi <ssid> [pass]   connect to a network (open network without pass; ssid "" = AP only)
//
// Words with spaces go in double quotes: wifi "There's no place like 127.0.0.1" "pass word".
// Inside quotes \" is a quote and \\ a backslash.
#pragma once

#include <cstdint>

#include "effects/Command.h"
#include "effects/EffectRegistry.h"
#include "state/Presets.h"

namespace ledframe {

enum class ConsoleAction : uint8_t {
  kNone,         // empty line
  kEngine,       // result.command goes to the engine
  kColorOrder,   // result.colorOrder goes to LedOutput
  kTestPattern,  // show the test pattern
  kInfo,         // print the state
  kPresetSave,   // save a preset named result.presetName
  kPresetApply,  // apply preset result.presetId
  kWifi,         // connect to result.wifiSsid with result.wifiPassword
  kError,        // result.error explains the problem
};

struct ConsoleResult {
  ConsoleAction action = ConsoleAction::kNone;
  Command command;
  uint8_t colorOrder = 0;
  char presetName[Preset::kNameMax + 1] = "";
  uint8_t presetId = 0;
  char wifiSsid[33] = "";
  char wifiPassword[65] = "";
  const char* error = nullptr;
};

// Maximum accepted line length, without the terminating zero. Fits
// "wifi <32-byte ssid> <64-byte password>".
inline constexpr uint8_t kConsoleLineMax = 127;

// Text listing all commands, printed after an unknown command.
extern const char kConsoleHelp[];

// Parses one line. activeEffect is the registry index of the active effect (or
// 0xFF); "param" resolves parameter ids against that effect and targets it
// explicitly, so a later effect switch cannot redirect the value.
ConsoleResult parseConsoleLine(const char* line, const EffectRegistry& registry,
                               uint8_t activeEffect);

}  // namespace ledframe

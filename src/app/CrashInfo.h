// Reset reason and last crash, read once at start (plain data, native-safe).
#pragma once

#include <cstdint>

namespace ledframe {

struct CrashInfo {
  static constexpr uint8_t kMaxBacktrace = 16;

  const char* resetReason = "unknown";  // "power-on", "software", "panic", ...
  bool hasCoredump = false;             // a core dump was found in flash at start
  char task[17] = "";                   // task that crashed
  uint32_t pc = 0;                      // program counter of the exception
  uint8_t depth = 0;                    // backtrace entries
  uint32_t backtrace[kMaxBacktrace] = {};
  bool backtraceCorrupted = false;
};

}  // namespace ledframe

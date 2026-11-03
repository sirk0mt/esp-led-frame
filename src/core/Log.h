// Logging macros shared by firmware and host tests.
//
// Usage: LOG_I("wifi", "connected to %s", ssid);
// The format must be a string literal. Arguments must not have side effects:
// a disabled level never evaluates them, so logs can never carry program logic.
#pragma once

#include <cstdio>

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// The level check is a compile-time constant, so disabled calls are removed by the
// compiler while their format strings are still type-checked.
#define LEDFRAME_LOG(level, letter, tag, fmt, ...)                     \
  do {                                                                 \
    if (LOG_LEVEL >= (level)) {                                        \
      std::printf("[" letter "][%s] " fmt "\n", (tag), ##__VA_ARGS__); \
    }                                                                  \
  } while (0)

#define LOG_E(tag, fmt, ...) LEDFRAME_LOG(LOG_LEVEL_ERROR, "E", tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) LEDFRAME_LOG(LOG_LEVEL_WARN, "W", tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) LEDFRAME_LOG(LOG_LEVEL_INFO, "I", tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) LEDFRAME_LOG(LOG_LEVEL_DEBUG, "D", tag, fmt, ##__VA_ARGS__)

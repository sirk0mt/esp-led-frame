// Firmware version and build information.
#pragma once

#if !defined(ESP32) && !defined(LEDFRAME_NATIVE)
#error "esp-led-frame supports ESP32 only. Host tests must define LEDFRAME_NATIVE."
#endif

namespace ledframe {

inline constexpr char kVersion[] = "2.2.0";
inline constexpr char kBuildDate[] = __DATE__ " " __TIME__;

}  // namespace ledframe

// Firmware version and build information.
#pragma once

#if !defined(ESP32) && !defined(LEDFRAME_NATIVE)
#error "esp-led-frame supports ESP32 only. Host tests must define LEDFRAME_NATIVE."
#endif

// Build variant: the PlatformIO environment whose firmware this is. The web UI checks
// GitHub for a newer release and links to its file esp-led-frame-<version>-<variant>-ota.bin
// (GET /api/info: "variant", "repo"). Set in platformio.ini.
#if !defined(LEDFRAME_VARIANT)
#define LEDFRAME_VARIANT "esp32dev"
#endif

namespace ledframe {

inline constexpr char kVersion[] = "2.3.2";
inline constexpr char kBuildDate[] = __DATE__ " " __TIME__;
inline constexpr char kFirmwareVariant[] = LEDFRAME_VARIANT;
// GitHub repository "owner/name" with the releases of ci.yml (job "release").
inline constexpr char kGithubRepo[] = "sirk0mt/esp-led-frame";

}  // namespace ledframe

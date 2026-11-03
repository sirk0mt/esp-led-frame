// Wi-Fi status types shared by WifiManager and the API (plain data, native-safe).
#pragma once

#include <cstdint>

namespace ledframe {

enum class WifiMode : uint8_t { kOff, kConnecting, kConnected, kAccessPoint };

// Name for logs and the API: "off", "connecting", "connected", "ap".
inline const char* wifiModeName(WifiMode mode) {
  switch (mode) {
    case WifiMode::kOff:
      return "off";
    case WifiMode::kConnecting:
      return "connecting";
    case WifiMode::kConnected:
      return "connected";
    case WifiMode::kAccessPoint:
      return "ap";
  }
  return "?";
}

struct WifiStatus {
  WifiMode mode = WifiMode::kOff;
  char ssid[33] = "";    // configured network
  char ip[16] = "";      // station IP when connected, AP IP in AP mode
  int8_t rssi = 0;       // when connected
  char apName[20] = "";  // "LedFrame-XXXX"
  bool apRetrying = false;
  uint8_t apClients = 0;
};

struct WifiNetwork {
  char ssid[33] = "";
  int8_t rssi = 0;
  bool secure = false;
};

}  // namespace ledframe

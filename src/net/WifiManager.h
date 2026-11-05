// Wi-Fi: station with timeout, access point fallback with captive DNS, background
// reconnect, mDNS and asynchronous scan (ESP32 only).
//
// tick() drives a non-blocking state machine and must be called often (every
// 10 ms from the Arduino loop task) because it also serves the captive DNS.
// requestConnect(), requestScan(), scanResults() and status() are thread-safe, so
// network handlers can call them from other tasks.
//
//   Connecting --connected--> Connected --link lost--> Connecting (2 min, background)
//   Connecting --timeout----> AccessPoint "LedFrame-XXXX" at 192.168.4.1
//   AccessPoint: every 5 min the station is tried again while the AP keeps running.
//     A try scans every channel and takes the radio away from the AP, so it waits
//     while a client is connected (someone uses the setup page), at most 30 min.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>

#include "net/WifiTypes.h"

namespace ledframe {

class WifiManager {
 public:
  static constexpr uint32_t kConnectTimeoutMs = 20000;
  static constexpr uint32_t kReconnectTimeoutMs = 120000;
  static constexpr uint32_t kApRetryIntervalMs = 300000;
  static constexpr uint32_t kApRetryTimeoutMs = 15000;
  static constexpr uint32_t kApBusyRetryMs = 1800000;  // longest wait while AP clients stay
  static constexpr uint8_t kMaxScanResults = 20;

  // Starts connecting, or the access point right away when ssid is empty.
  void begin(const char* hostname, const char* ssid, const char* password);
  // Arduino loop task only.
  void tick(uint32_t nowMs);

  // Thread-safe. New credentials take effect in the next tick(). In AP mode the
  // access point stays up while the station tries them.
  void requestConnect(const char* ssid, const char* password);
  // Thread-safe. Returns false while a scan is already running.
  bool requestScan();
  bool scanRunning() const;
  // Thread-safe copy of the last scan, strongest first. Returns the number copied.
  uint8_t scanResults(WifiNetwork* out, uint8_t capacity) const;
  WifiStatus status() const;

 private:
  void applyRequests(uint32_t nowMs);
  void pollScan();
  void startStation(uint32_t nowMs, uint32_t timeoutMs);
  void startAccessPoint(uint32_t nowMs);
  void stopAccessPoint();
  void startApRetry(uint32_t nowMs);
  void onConnected();
  void keepIpv4Dns(uint32_t nowMs);
  void publish(uint32_t nowMs, bool force);

  // Loop task only.
  WifiMode mode_ = WifiMode::kOff;
  char hostname_[33] = "";
  char ssid_[33] = "";
  char password_[65] = "";
  bool apRunning_ = false;
  bool staStarted_ = false;  // WiFi.begin() was called since the last disconnect
  bool apRetrying_ = false;
  bool mdnsStarted_ = false;
  bool scanning_ = false;
  uint32_t deadlineMs_ = 0;
  uint32_t nextRetryMs_ = 0;
  uint32_t lastRetryMs_ = 0;  // last try of the station in AP mode, or the AP start
  uint32_t lastPublishMs_ = 0;
  char apName_[20] = "";
  uint32_t dhcpDns_[2] = {};  // IPv4 DNS servers from DHCP at the last connection, 0 = none
  uint32_t nextDnsCheckMs_ = 0;
  bool dnsRestoreLogged_ = false;  // keepIpv4Dns() logged since the last connection

  // Shared with other tasks, guarded by mutex_.
  SemaphoreHandle_t mutex_ = nullptr;
  bool connectRequested_ = false;
  char requestedSsid_[33] = "";
  char requestedPassword_[65] = "";
  bool scanRequested_ = false;
  bool scanBusy_ = false;
  WifiNetwork results_[kMaxScanResults];
  uint8_t resultCount_ = 0;
  WifiStatus status_;
};

}  // namespace ledframe

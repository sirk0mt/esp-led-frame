#include "net/WifiManager.h"

#include <AsyncUDP.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>

#include "core/Log.h"
#include "net/DnsMessage.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "wifi";
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kStatusRefreshMs = 1000;
const IPAddress kApAddress(192, 168, 4, 1);
const IPAddress kApNetmask(255, 255, 255, 0);

// Captive DNS: every name resolves to the access point (DnsMessage.h). Our own
// instead of the Arduino DNSServer, so debug builds can log what clients ask.
AsyncUDP dnsUdp;

void startDns() {
  dnsUdp.onPacket([](AsyncUDPPacket& packet) {
    static const uint8_t kAddress[4] = {192, 168, 4, 1};
    uint8_t reply[600];
    DnsQuestion question;
    const size_t length =
        buildDnsReply(packet.data(), packet.length(), kAddress, reply, sizeof(reply), question);
    if (length > 0) {
      packet.write(reply, length);
    }
    LOG_D(kTag, "DNS %s asks %s (type %u)%s", packet.remoteIP().toString().c_str(), question.name,
          static_cast<unsigned>(question.type), length > 0 ? "" : ", not answered");
  });
  dnsUdp.listen(kDnsPort);
}

void stopDns() {
  dnsUdp.close();
}

template <size_t N>
void copyText(char (&out)[N], const char* text) {
  std::snprintf(out, N, "%s", text != nullptr ? text : "");
}

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex_); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  SemaphoreHandle_t mutex_;
};

}  // namespace

void WifiManager::begin(const char* hostname, const char* ssid, const char* password) {
  mutex_ = xSemaphoreCreateMutex();
  copyText(hostname_, hostname);
  copyText(ssid_, ssid);
  copyText(password_, password);

  // Read from eFuse: WiFi.macAddress() returns zeros before the driver starts.
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  std::snprintf(apName_, sizeof(apName_), "LedFrame-%02X%02X", mac[4], mac[5]);

  WiFi.persistent(false);  // Credentials live in /config.json, not in the Wi-Fi NVS.
  WiFi.setHostname(hostname_);
  // No modem sleep. In AP+STA mode the station's power save makes the access point
  // lose frames to clients that sleep themselves: phones got DNS queries through,
  // but almost no TCP connection. The device runs on mains power, and
  // the radio staying awake also shortens the delay of UI changes.
  WiFi.setSleep(false);
  // Clients of the access point, to diagnose the captive portal from the serial log.
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      LOG_I(kTag, "AP client %02x:%02x:%02x:%02x:%02x:%02x joined", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      LOG_I(kTag, "AP client %02x:%02x:%02x:%02x:%02x:%02x left", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
      LOG_I(kTag, "AP client got %s", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
    }
  });
  const uint32_t now = millis();
  if (ssid_[0] == '\0') {
    LOG_I(kTag, "no network configured");
    startAccessPoint(now);
  } else {
    startStation(now, kConnectTimeoutMs);
  }
  publish(now, true);
}

void WifiManager::tick(uint32_t nowMs) {
  if (mutex_ == nullptr) {
    return;
  }
  applyRequests(nowMs);
  pollScan();

  const bool linked = WiFi.status() == WL_CONNECTED;
  switch (mode_) {
    case WifiMode::kOff:
      break;
    case WifiMode::kConnecting:
      if (linked) {
        onConnected();
      } else if (static_cast<int32_t>(nowMs - deadlineMs_) >= 0) {
        LOG_W(kTag, "cannot connect to '%s'", ssid_);
        startAccessPoint(nowMs);
      }
      break;
    case WifiMode::kConnected:
      if (!linked) {
        // The driver reconnects by itself; fall back to the AP only after a while.
        LOG_W(kTag, "connection lost, reconnecting");
        mode_ = WifiMode::kConnecting;
        deadlineMs_ = nowMs + kReconnectTimeoutMs;
      }
      break;
    case WifiMode::kAccessPoint:
      if (apRetrying_) {
        if (linked) {
          onConnected();
        } else if (static_cast<int32_t>(nowMs - deadlineMs_) >= 0) {
          LOG_I(kTag, "network '%s' still unreachable", ssid_);
          WiFi.disconnect(false);
          staStarted_ = false;
          apRetrying_ = false;
          nextRetryMs_ = nowMs + kApRetryIntervalMs;
        }
      } else if (ssid_[0] != '\0' && static_cast<int32_t>(nowMs - nextRetryMs_) >= 0 &&
                 (WiFi.softAPgetStationNum() == 0 || nowMs - lastRetryMs_ >= kApBusyRetryMs)) {
        startApRetry(nowMs);
      }
      break;
  }
  publish(nowMs, false);
}

void WifiManager::requestConnect(const char* ssid, const char* password) {
  if (mutex_ == nullptr) {
    return;
  }
  Lock lock(mutex_);
  copyText(requestedSsid_, ssid);
  copyText(requestedPassword_, password);
  connectRequested_ = true;
}

bool WifiManager::requestScan() {
  if (mutex_ == nullptr) {
    return false;
  }
  Lock lock(mutex_);
  if (scanBusy_ || scanRequested_) {
    return false;
  }
  scanRequested_ = true;
  return true;
}

bool WifiManager::scanRunning() const {
  if (mutex_ == nullptr) {
    return false;
  }
  Lock lock(mutex_);
  return scanBusy_ || scanRequested_;
}

uint8_t WifiManager::scanResults(WifiNetwork* out, uint8_t capacity) const {
  if (mutex_ == nullptr || out == nullptr) {
    return 0;
  }
  Lock lock(mutex_);
  const uint8_t count = resultCount_ < capacity ? resultCount_ : capacity;
  for (uint8_t i = 0; i < count; ++i) {
    out[i] = results_[i];
  }
  return count;
}

WifiStatus WifiManager::status() const {
  if (mutex_ == nullptr) {
    return WifiStatus{};
  }
  Lock lock(mutex_);
  return status_;
}

void WifiManager::applyRequests(uint32_t nowMs) {
  bool connect = false;
  bool scan = false;
  {
    Lock lock(mutex_);
    if (connectRequested_) {
      connectRequested_ = false;
      connect = true;
      copyText(ssid_, requestedSsid_);
      copyText(password_, requestedPassword_);
    }
    if (scanRequested_ && !scanning_) {
      scanRequested_ = false;
      scanBusy_ = true;
      scan = true;
    }
  }
  if (connect) {
    LOG_I(kTag, "new network '%s'", ssid_);
    if (ssid_[0] == '\0') {
      startAccessPoint(nowMs);
    } else if (mode_ == WifiMode::kAccessPoint) {
      startApRetry(nowMs);  // Keep the AP so a phone on the portal stays connected.
    } else {
      startStation(nowMs, kConnectTimeoutMs);
    }
    publish(nowMs, true);
  }
  if (scan) {
    // Scanning needs the station interface; AP mode already runs AP+STA.
    if (WiFi.getMode() == WIFI_MODE_AP) {
      WiFi.mode(WIFI_AP_STA);
    }
    const int16_t result = WiFi.scanNetworks(true, false);
    scanning_ = result == WIFI_SCAN_RUNNING || result >= 0;
    if (!scanning_) {
      LOG_W(kTag, "scan did not start");
      Lock lock(mutex_);
      scanBusy_ = false;
    }
  }
}

void WifiManager::pollScan() {
  if (!scanning_) {
    return;
  }
  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    return;
  }
  scanning_ = false;
  WifiNetwork networks[kMaxScanResults];
  uint8_t count = 0;
  for (int16_t i = 0; i < found; ++i) {
    WifiNetwork network;
    copyText(network.ssid, WiFi.SSID(i).c_str());
    if (network.ssid[0] == '\0') {
      continue;  // Hidden network.
    }
    network.rssi = static_cast<int8_t>(WiFi.RSSI(i));
    network.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    // Insert by signal strength, keep the strongest kMaxScanResults.
    uint8_t position = count;
    while (position > 0 && networks[position - 1].rssi < network.rssi) {
      if (position < kMaxScanResults) {
        networks[position] = networks[position - 1];
      }
      --position;
    }
    if (position < kMaxScanResults) {
      networks[position] = network;
      if (count < kMaxScanResults) {
        ++count;
      }
    }
  }
  WiFi.scanDelete();
  LOG_I(kTag, "scan found %u networks", static_cast<unsigned>(count));

  Lock lock(mutex_);
  for (uint8_t i = 0; i < count; ++i) {
    results_[i] = networks[i];
  }
  resultCount_ = count;
  scanBusy_ = false;
}

void WifiManager::startStation(uint32_t nowMs, uint32_t timeoutMs) {
  stopAccessPoint();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);
  staStarted_ = true;
  mode_ = WifiMode::kConnecting;
  deadlineMs_ = nowMs + timeoutMs;
  LOG_I(kTag, "connecting to '%s'", ssid_);
}

void WifiManager::startAccessPoint(uint32_t nowMs) {
  WiFi.mode(WIFI_AP_STA);  // STA stays available for scans and retries.
  WiFi.setAutoReconnect(false);
  if (staStarted_) {
    WiFi.disconnect(false);  // Stop the failed station attempt; the AP keeps running.
    staStarted_ = false;
  }
  WiFi.softAPConfig(kApAddress, kApAddress, kApNetmask);
  WiFi.softAP(apName_);
  startDns();
  apRunning_ = true;
  apRetrying_ = false;
  mode_ = WifiMode::kAccessPoint;
  nextRetryMs_ = nowMs + kApRetryIntervalMs;
  lastRetryMs_ = nowMs;
  LOG_I(kTag, "access point '%s' at 192.168.4.1", apName_);
}

void WifiManager::stopAccessPoint() {
  if (!apRunning_) {
    return;
  }
  stopDns();
  WiFi.softAPdisconnect(true);
  apRunning_ = false;
  apRetrying_ = false;
}

void WifiManager::startApRetry(uint32_t nowMs) {
  LOG_I(kTag, "trying '%s' again", ssid_);
  WiFi.begin(ssid_, password_);
  staStarted_ = true;
  apRetrying_ = true;
  deadlineMs_ = nowMs + kApRetryTimeoutMs;
  lastRetryMs_ = nowMs;
}

void WifiManager::onConnected() {
  stopAccessPoint();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  mode_ = WifiMode::kConnected;
  LOG_I(kTag, "connected to '%s', IP %s", ssid_, WiFi.localIP().toString().c_str());
  if (!mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(hostname_);
    if (mdnsStarted_) {
      MDNS.addService("http", "tcp", 80);
      LOG_I(kTag, "mDNS %s.local", hostname_);
    } else {
      LOG_W(kTag, "mDNS did not start");
    }
  }
}

void WifiManager::publish(uint32_t nowMs, bool force) {
  if (!force && nowMs - lastPublishMs_ < kStatusRefreshMs) {
    return;
  }
  lastPublishMs_ = nowMs;
  WifiStatus next;
  next.mode = mode_;
  copyText(next.ssid, ssid_);
  copyText(next.apName, apName_);
  next.apRetrying = apRetrying_;
  if (mode_ == WifiMode::kConnected) {
    copyText(next.ip, WiFi.localIP().toString().c_str());
    next.rssi = static_cast<int8_t>(WiFi.RSSI());
  } else if (mode_ == WifiMode::kAccessPoint) {
    copyText(next.ip, WiFi.softAPIP().toString().c_str());
    next.apClients = static_cast<uint8_t>(WiFi.softAPgetStationNum());
  }
  Lock lock(mutex_);
  status_ = next;
}

}  // namespace ledframe

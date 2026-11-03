// Firmware updates over the network (ESP32 only).
//
// - POST /api/ota: multipart upload of firmware.bin (curl -F firmware=@firmware.bin).
//   When an OTA password is configured, the request needs HTTP Basic auth with the
//   user "ota". The update switches the LEDs off and writes pending files first,
//   answers when the image is verified and restarts afterwards.
// - ArduinoOTA (espota, env:esp32dev-ota) on UDP port 3232 with the same password.
//   It runs inside the Arduino loop task and blocks it while the upload runs.
#pragma once

class AsyncWebServer;

namespace ledframe {

class App;

class Ota {
 public:
  // Registers POST /api/ota. Call before server.begin().
  void attach(AsyncWebServer& server, App& app);
  // Starts ArduinoOTA. A new password takes effect after a restart.
  void beginArduinoOta(App& app, const char* hostname, const char* password);
  // Arduino loop task.
  void tick();
};

}  // namespace ledframe

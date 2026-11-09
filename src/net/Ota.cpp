#include "net/Ota.h"

#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

#include <cstdio>

#include "app/App.h"
#include "core/Log.h"
#include "net/ApiJson.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "ota";
constexpr char kJson[] =
    "application/json; charset=utf-8";  // PowerShell 5.1 assumes ISO-8859-1 without a charset
constexpr char kOtaUser[] = "ota";
constexpr uint32_t kRestartDelayMs = 1000;  // lets the response leave before the restart

App* app = nullptr;
bool arduinoOtaStarted = false;

// One HTTP upload at a time. Touched only by the AsyncTCP task.
struct Upload {
  AsyncWebServerRequest* owner = nullptr;
  bool running = false;  // Update.begin() succeeded, image not finished yet
  bool done = false;     // Update.end() verified the image
  int status = 0;        // HTTP error status, 0 = no error
  const char* error = "";
  size_t bytes = 0;
};

Upload upload;

void sendError(AsyncWebServerRequest* request, int status, const char* message) {
  char buffer[160];
  if (writeError(message, buffer, sizeof(buffer)) == 0) {
    request->send(status, kJson, "{\"error\":\"update failed\"}");
    return;
  }
  request->send(status, kJson, buffer);
}

void fail(int status, const char* message) {
  upload.status = status;
  upload.error = message;
  if (upload.running) {
    Update.abort();
    upload.running = false;
    app->cancelOta();
  }
  LOG_E(kTag, "update failed: %s", message);
}

bool authorized(AsyncWebServerRequest* request) {
  const Config config = app->config();
  if (config.otaPassword[0] == '\0') {
    return true;
  }
  return request->authenticate(kOtaUser, config.otaPassword);
}

void onUpload(AsyncWebServerRequest* request, const String& /*filename*/, size_t index,
              uint8_t* data, size_t length, bool final) {
  if (index == 0) {
    if (upload.running) {
      return;  // Another upload is running; onRequest answers 409.
    }
    upload = Upload{};
    upload.owner = request;
    // The server middleware refuses requests from other sites only after the whole
    // body arrived, when the image would already be in the flash (WebServer.cpp).
    const AsyncWebHeader* origin = request->getHeader("Origin");
    const char* refusal = refuseRequest(origin != nullptr ? origin->value().c_str() : nullptr,
                                        request->host().c_str(), app->hostname());
    if (refusal != nullptr) {
      upload.status = 403;
      upload.error = refusal;
      LOG_W(kTag, "update rejected: %s", upload.error);
      return;
    }
    if (!authorized(request)) {
      upload.status = 401;
      upload.error = "wrong or missing OTA password";
      LOG_W(kTag, "update rejected: %s", upload.error);
      return;
    }
    // The new image goes to the other partition, where the previous firmware is. A
    // running firmware still on trial would have nothing to roll back to, and an
    // update that reaches it works, so it is kept first (App::confirmFirmware()).
    app->confirmFirmware("another update started");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      fail(500, Update.errorString());
      return;
    }
    upload.running = true;
    app->beginOta();  // LEDs off, rendering paused, pending files written.
    request->onDisconnect([]() {
      if (upload.running) {
        LOG_W(kTag, "client disconnected during the update");
        Update.abort();
        upload.running = false;
        app->cancelOta();
      }
      upload.owner = nullptr;
    });
    LOG_I(kTag, "update started");
  }
  if (upload.owner != request || !upload.running) {
    return;
  }
  if (length > 0 && Update.write(data, length) != length) {
    // A wrong first byte is reported as a magic byte or (with encrypted OTA support)
    // a decryption error: the file is not an ESP32 image, a client error.
    const uint8_t code = Update.getError();
    if (code == UPDATE_ERROR_MAGIC_BYTE || code == UPDATE_ERROR_DECRYPT) {
      fail(400, "not an ESP32 firmware image");
    } else {
      fail(500, Update.errorString());
    }
    return;
  }
  upload.bytes += length;
  if (final) {
    upload.running = false;
    if (!Update.end(true)) {
      fail(400, Update.errorString());  // Truncated or not a firmware image.
      app->cancelOta();
      return;
    }
    upload.done = true;
    LOG_I(kTag, "update written: %u bytes", static_cast<unsigned>(upload.bytes));
  }
}

void onRequest(AsyncWebServerRequest* request) {
  if (upload.owner != request) {
    if (upload.running) {
      sendError(request, 409, "another update is running");
    } else {
      sendError(request, 400, "send the firmware as multipart field 'firmware'");
    }
    return;
  }
  if (upload.status != 0) {
    const int status = upload.status;
    const char* message = upload.error;
    upload = Upload{};
    if (status == 401) {
      AsyncWebServerResponse* response =
          request->beginResponse(401, kJson, "{\"error\":\"wrong or missing OTA password\"}");
      // A browser answers WWW-Authenticate with its own login dialog and holds the
      // request until it closes; the web UI marks its requests to get a plain 401.
      if (!request->hasHeader("X-Requested-With")) {
        response->addHeader("WWW-Authenticate", "Basic realm=\"ota\"");
      }
      request->send(response);
      return;
    }
    sendError(request, status, message);
    return;
  }
  if (!upload.done) {
    fail(400, "incomplete upload");
    upload = Upload{};
    sendError(request, 400, "incomplete upload");
    return;
  }
  char response[64];
  std::snprintf(response, sizeof(response), "{\"ok\":true,\"bytes\":%u,\"restarting\":true}",
                static_cast<unsigned>(upload.bytes));
  upload = Upload{};
  request->send(200, kJson, response);
  app->requestRestart(kRestartDelayMs);  // Writes pending files again, then restarts.
}

}  // namespace

void Ota::attach(AsyncWebServer& server, App& target) {
  app = &target;
  server.on(AsyncURIMatcher::exact("/api/ota"), HTTP_POST, onRequest, onUpload);
}

void Ota::beginArduinoOta(App& target, const char* hostname, const char* password) {
  app = &target;
  ArduinoOTA.setHostname(hostname);
  if (password != nullptr && password[0] != '\0') {
    ArduinoOTA.setPassword(password);
  }
  ArduinoOTA.setMdnsEnabled(false);  // WifiManager runs mDNS for <hostname>.local.
  ArduinoOTA.onStart([]() {
    LOG_I(kTag, "ArduinoOTA update started");
    // Called after Update.begin() but before the first write: see onUpload().
    app->confirmFirmware("another update started");
    app->beginOta();
    app->flushNow();  // ArduinoOTA blocks the loop task until it is done.
  });
  ArduinoOTA.onEnd([]() { LOG_I(kTag, "ArduinoOTA update written, restarting"); });
  ArduinoOTA.onError([](ota_error_t error) {
    LOG_E(kTag, "ArduinoOTA error %u", static_cast<unsigned>(error));
    app->cancelOta();
  });
  ArduinoOTA.begin();
  arduinoOtaStarted = true;
  LOG_I(kTag, "ArduinoOTA on port 3232%s",
        password != nullptr && password[0] != '\0' ? " with password" : "");
}

void Ota::tick() {
  if (arduinoOtaStarted) {
    ArduinoOTA.handle();
  }
}

}  // namespace ledframe

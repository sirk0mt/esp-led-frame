#include "net/WebServer.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>

#include "app/App.h"
#include "app/CrashReport.h"
#include "core/Log.h"
#include "core/Version.h"
#include "effects/EffectRegistry.h"
#include "effects/PaintImage.h"
#include "generated/web_assets.h"
#include "net/ApiJson.h"
#include "net/Ota.h"
#include "net/WsHub.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "http";
constexpr char kJson[] =
    "application/json; charset=utf-8";  // PowerShell 5.1 assumes ISO-8859-1 without a charset
constexpr size_t kResponseMax = 12288;  // upper limit; buffers have the response's size
constexpr size_t kStateBodyMax = 2048;
constexpr size_t kConfigBodyMax = 1024;
constexpr size_t kPresetBodyMax = 256;
constexpr uint8_t kPaletteSamples = 16;
constexpr char kApAddress[] = "192.168.4.1";

AsyncWebServer server(80);
App* app = nullptr;

// --- Web UI files (web/, embedded by scripts/embed_web.py) --------------------

// Sends an embedded file, or 304 when the browser already has this version. The
// browser must revalidate every time (no-cache), so a firmware update with a new
// UI shows at once; an unchanged file costs only the 304.
void sendAsset(AsyncWebServerRequest* request, const web::WebAsset& asset) {
  const AsyncWebHeader* match = request->getHeader("If-None-Match");
  // strstr also covers a list of tags and the weak form W/"...".
  if (match != nullptr && std::strstr(match->value().c_str(), asset.etag) != nullptr) {
    AsyncWebServerResponse* response = request->beginResponse(304);
    response->addHeader("ETag", asset.etag);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
    return;
  }
  AsyncWebServerResponse* response =
      request->beginResponse(200, asset.mime, asset.data, asset.size);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("ETag", asset.etag);
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

const web::WebAsset* findAsset(const char* path) {
  for (const web::WebAsset& asset : web::kWebAssets) {
    if (std::strcmp(asset.path, path) == 0) {
      return &asset;
    }
  }
  return nullptr;
}

void serveAssets() {
  using M = AsyncURIMatcher;
  for (const web::WebAsset& asset : web::kWebAssets) {
    const web::WebAsset* file = &asset;
    server.on(M::exact(asset.path), HTTP_GET,
              [file](AsyncWebServerRequest* request) { sendAsset(request, *file); });
  }
  const web::WebAsset* index = findAsset("/index.html");
  if (index == nullptr) {
    LOG_E(kTag, "web/index.html is missing from the firmware");
    return;
  }
  server.on(M::exact("/"), HTTP_GET,
            [index](AsyncWebServerRequest* request) { sendAsset(request, *index); });
  LOG_I(kTag, "%u web files embedded", static_cast<unsigned>(web::kWebAssetCount));
}

void sendError(AsyncWebServerRequest* request, int status, const char* message) {
  char buffer[128];
  if (writeError(message, buffer, sizeof(buffer)) == 0) {
    request->send(status, kJson, "{\"error\":\"error\"}");
    return;
  }
  request->send(status, kJson, buffer);
}

void sendOk(AsyncWebServerRequest* request, int status = 200) {
  request->send(status, kJson, "{\"ok\":true}");
}

// Response with the first `length` bytes of buffer. The response is sent after the
// handler returns, so the filler keeps the buffer alive until then.
AsyncWebServerResponse* bufferResponse(AsyncWebServerRequest* request, const char* type,
                                       const std::shared_ptr<uint8_t>& buffer, size_t length) {
  return request->beginResponse(
      type, length, [buffer, length](uint8_t* out, size_t capacity, size_t index) -> size_t {
        const size_t count = length - index < capacity ? length - index : capacity;
        std::memcpy(out, buffer.get() + index, count);
        return count;
      });
}

// Two passes: write(nullptr, 0) measures the response, the second call fills a heap
// buffer of exactly that size. The response sends that buffer without copying it
// into a String, and nothing large sits on the AsyncTCP task stack.
void sendJson(AsyncWebServerRequest* request, int status,
              const std::function<size_t(char*, size_t)>& write) {
  const size_t length = write(nullptr, 0);
  if (length == 0 || length >= kResponseMax) {
    sendError(request, 500, "response too large");
    return;
  }
  std::shared_ptr<uint8_t> buffer(new (std::nothrow) uint8_t[length + 1],
                                  std::default_delete<uint8_t[]>());
  if (!buffer) {
    sendError(request, 503, "out of memory");
    return;
  }
  if (write(reinterpret_cast<char*>(buffer.get()), length + 1) != length) {
    sendError(request, 500, "response changed while writing");
    return;
  }
  AsyncWebServerResponse* response = bufferResponse(request, kJson, buffer, length);
  response->setCode(status);
  request->send(response);
}

// Collects the body into request->_tempObject (freed by the request). Bodies larger
// than limit are not stored; the handler answers 413.
ArBodyHandlerFunction collectBody(size_t limit) {
  return [limit](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index,
                 size_t total) {
    if (total > limit) {
      return;
    }
    if (index == 0) {
      request->_tempObject = std::malloc(total + 1);
    }
    char* buffer = static_cast<char*>(request->_tempObject);
    if (buffer == nullptr || index + length > total) {
      return;
    }
    std::memcpy(buffer + index, data, length);
    if (index + length == total) {
      buffer[total] = '\0';
    }
  };
}

// Returns false after answering with an error.
bool takeBody(AsyncWebServerRequest* request, size_t limit, const char*& body, size_t& length) {
  if (request->contentLength() > limit) {
    sendError(request, 413, "body too large");
    return false;
  }
  body = static_cast<const char*>(request->_tempObject);
  length = request->contentLength();
  if (body == nullptr) {
    // A form-encoded body (curl -d without a Content-Type) bypasses the body handler:
    // the library stores a body starting with '{' as the POST parameter "body".
    const AsyncWebParameter* param = request->getParam("body", true);
    if (param != nullptr) {
      body = param->value().c_str();
      length = param->value().length();
    }
  }
  if (body == nullptr || length == 0) {
    sendError(request, 400, "empty body");
    return false;
  }
  return true;
}

void logIgnored(AsyncWebServerRequest* request, const IgnoredFields& ignored) {
  for (uint8_t i = 0; i < ignored.count && i < IgnoredFields::kMaxNames; ++i) {
    LOG_W(kTag, "%s: unknown field '%s' ignored", request->url().c_str(), ignored.names[i]);
  }
}

// --- State, effects, palettes ------------------------------------------------

void getState(AsyncWebServerRequest* request) {
  const StateSnapshot state = app->snapshot();
  sendJson(request, 200, [&](char* out, size_t capacity) {
    return writeState(state, EffectRegistry::global(), out, capacity);
  });
}

void postState(AsyncWebServerRequest* request) {
  const char* body = nullptr;
  size_t length = 0;
  if (!takeBody(request, kStateBodyMax, body, length)) {
    return;
  }
  const StateSnapshot state = app->snapshot();
  std::unique_ptr<CommandBatch> batch(new (std::nothrow) CommandBatch);
  if (!batch) {
    sendError(request, 503, "out of memory");
    return;
  }
  ApiError error;
  IgnoredFields ignored;
  if (!parseStatePatch(body, length, EffectRegistry::global(), state.effect, *batch, error,
                       &ignored)) {
    sendError(request, error.status, error.message);
    return;
  }
  logIgnored(request, ignored);
  for (uint8_t i = 0; i < batch->count; ++i) {
    if (!app->post(batch->commands[i])) {
      sendError(request, 503, "command queue full");
      return;
    }
  }
  sendOk(request);
}

void getEffects(AsyncWebServerRequest* request) {
  sendJson(request, 200, [](char* out, size_t capacity) {
    return writeEffects(EffectRegistry::global(), out, capacity);
  });
}

void getPalettes(AsyncWebServerRequest* request) {
  sendJson(request, 200, [](char* out, size_t capacity) {
    return writePalettes(app->palettes(), kPaletteSamples, out, capacity);
  });
}

// The picture in the PaintImage binary format (docs/API.md).
void getPaint(AsyncWebServerRequest* request) {
  std::shared_ptr<uint8_t> buffer(new (std::nothrow) uint8_t[PaintImage::kMaxBytes],
                                  std::default_delete<uint8_t[]>());
  if (!buffer) {
    sendError(request, 503, "out of memory");
    return;
  }
  const size_t length = app->copyPaint(buffer.get(), PaintImage::kMaxBytes);
  if (length == 0) {
    sendError(request, 500, "no picture");
    return;
  }
  AsyncWebServerResponse* response =
      bufferResponse(request, "application/octet-stream", buffer, length);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

// --- Configuration -----------------------------------------------------------

void getConfig(AsyncWebServerRequest* request) {
  const Config config = app->config();
  sendJson(request, 200,
           [&](char* out, size_t capacity) { return writeConfig(config, out, capacity); });
}

void postConfig(AsyncWebServerRequest* request) {
  const char* body = nullptr;
  size_t length = 0;
  if (!takeBody(request, kConfigBodyMax, body, length)) {
    return;
  }
  Config config = app->config();
  ApiError error;
  IgnoredFields ignored;
  if (!parseConfigPatch(body, length, config, error, &ignored)) {
    sendError(request, error.status, error.message);
    return;
  }
  logIgnored(request, ignored);
  if (!app->applyConfig(config)) {
    sendError(request, 503, "command queue full");
    return;
  }
  sendOk(request);
}

// --- Presets -----------------------------------------------------------------

void getPresets(AsyncWebServerRequest* request) {
  std::unique_ptr<Presets> presets(new (std::nothrow) Presets);
  if (!presets) {
    sendError(request, 503, "out of memory");
    return;
  }
  app->copyPresets(*presets);
  sendJson(request, 200, [&](char* out, size_t capacity) {
    return writePresets(*presets, EffectRegistry::global(), out, capacity);
  });
}

void sendPresetError(AsyncWebServerRequest* request, PresetError error) {
  switch (error) {
    case PresetError::kNone:
      sendOk(request);
      return;
    case PresetError::kNotFound:
      sendError(request, 404, "unknown preset");
      return;
    case PresetError::kInvalidName:
      sendError(request, 400, "invalid preset name");
      return;
    case PresetError::kInvalidEffect:
      sendError(request, 409, "no active effect");
      return;
    case PresetError::kFull:
      sendError(request, 409, "preset list or command queue full");
      return;
    case PresetError::kNameTaken:
      sendError(request, 409, "another preset has this name");
      return;
  }
  sendError(request, 500, "preset error");
}

void postPreset(AsyncWebServerRequest* request) {
  const char* body = nullptr;
  size_t length = 0;
  if (!takeBody(request, kPresetBodyMax, body, length)) {
    return;
  }
  char name[Preset::kNameMax + 1];
  ApiError error;
  if (!parsePresetName(body, length, name, error)) {
    sendError(request, error.status, error.message);
    return;
  }
  PresetError result = PresetError::kNone;
  const uint8_t id = app->savePreset(name, &result);
  if (id == 0) {
    sendPresetError(request, result);
    return;
  }
  char response[24];
  std::snprintf(response, sizeof(response), "{\"id\":%u}", static_cast<unsigned>(id));
  request->send(201, kJson, response);
}

// DELETE /api/presets/{id}, POST /api/presets/{id} with {"name"} (rename) and
// POST /api/presets/{id}/apply.
void presetItem(AsyncWebServerRequest* request) {
  uint8_t id = 0;
  const PresetRoute route = parsePresetPath(request->url().c_str(), id);
  if (route == PresetRoute::kItem && request->method() == HTTP_DELETE) {
    sendPresetError(request, app->deletePreset(id));
  } else if (route == PresetRoute::kItem && request->method() == HTTP_POST) {
    const char* body = nullptr;
    size_t length = 0;
    if (!takeBody(request, kPresetBodyMax, body, length)) {
      return;
    }
    char name[Preset::kNameMax + 1];
    ApiError error;
    if (!parsePresetName(body, length, name, error)) {
      sendError(request, error.status, error.message);
      return;
    }
    sendPresetError(request, app->renamePreset(id, name));
  } else if (route == PresetRoute::kApply && request->method() == HTTP_POST) {
    sendPresetError(request, app->applyPreset(id));
  } else {
    sendError(request, 404, "not found");
  }
}

// --- Wi-Fi and system --------------------------------------------------------

void postScan(AsyncWebServerRequest* request) {
  app->requestWifiScan();  // Already running is fine: the result comes either way.
  request->send(202, kJson, "{\"scanning\":true}");
}

void getScan(AsyncWebServerRequest* request) {
  std::unique_ptr<WifiNetwork[]> networks(new (std::nothrow) WifiNetwork[20]);
  if (!networks) {
    sendError(request, 503, "out of memory");
    return;
  }
  const uint8_t count = app->wifiScanResults(networks.get(), 20);
  const bool scanning = app->wifiScanRunning();
  sendJson(request, 200, [&](char* out, size_t capacity) {
    return writeScan(networks.get(), count, scanning, out, capacity);
  });
}

void getInfo(AsyncWebServerRequest* request) {
  const StateSnapshot state = app->snapshot();
  SystemInfo info;
  info.version = kVersion;
  info.variant = kFirmwareVariant;
  info.repo = kGithubRepo;
  info.buildDate = kBuildDate;
  info.uptimeS = millis() / 1000;
  info.freeHeap = ESP.getFreeHeap();
  info.minFreeHeap = ESP.getMinFreeHeap();
  info.ledCount = static_cast<uint16_t>(state.layout.width * state.layout.height);
  info.maxLeds = kMaxLeds;
  info.testPattern = state.testPattern;
  info.ledPin = app->ledPin();
  info.fps = state.fps;
  info.stats = state.stats;
  info.wifi = app->wifiStatus();
  info.crash = &crashReport();
  info.time = app->wallTime();
  info.sntp = app->sntpRunning();
  sendJson(request, 200,
           [&](char* out, size_t capacity) { return writeInfo(info, out, capacity); });
}

// {"on": true} shows the test pattern (white first LED, then red, green and blue
// thirds in strip order), {"on": false} returns to the effect.
void postTestPattern(AsyncWebServerRequest* request) {
  const char* body = nullptr;
  size_t length = 0;
  if (!takeBody(request, kPresetBodyMax, body, length)) {
    return;
  }
  bool on = false;
  ApiError error;
  if (!parseTestPattern(body, length, on, error)) {
    sendError(request, error.status, error.message);
    return;
  }
  if (!app->showTestPattern(on)) {
    sendError(request, 503, "command queue full");
    return;
  }
  sendOk(request);
}

void postRestart(AsyncWebServerRequest* request) {
  sendOk(request);
  app->requestRestart();
}

// --- Captive portal ------------------------------------------------------------
// In AP mode the DNS server answers every name with 192.168.4.1, and every request
// for another host is redirected to the setup page. Phones and laptops check their
// connection with such requests (Android /generate_204, iOS /hotspot-detect.html,
// Windows /connecttest.txt and /ncsi.txt), see the redirect instead of the expected
// answer and open the page by themselves. Requests for the device address or
// <hostname>.local pass, so the full UI keeps working in AP mode.

constexpr char kSetupUrl[] = "http://192.168.4.1/setup.html";

void captivePortal(AsyncWebServerRequest* request, ArMiddlewareNext next) {
  if (app->wifiStatus().mode != WifiMode::kAccessPoint) {
    next();
    return;
  }
  const bool device = isDeviceHost(request->host().c_str(), kApAddress, app->hostname());
  // Debug builds log every request in AP mode: what a phone asks shows why its
  // captive portal check passes or fails.
  LOG_D(kTag, "AP %s %s%s%s", request->client()->remoteIP().toString().c_str(),
        request->host().c_str(), request->url().c_str(), device ? "" : " -> setup");
  if (!device) {
    request->redirect(kSetupUrl);
    return;
  }
  next();
}

// --- Requests from other sites -------------------------------------------------------
// A web page open in the browser of someone in the same network must not control the
// device (refuseRequest() in ApiJson):
//   - CSRF and cross-site WebSocket: a POST with a text/plain body and a WebSocket
//     handshake need no CORS preflight, so the Origin header must be the device itself;
//   - DNS rebinding: a site whose name later points to the device is the same origin
//     for the browser, so the Host header must be a name of the device, for every
//     request: over plain HTTP a browser cannot be told from curl by its headers.
// Middlewares run after the body is received, so POST /api/ota checks this itself
// before it writes to the flash (Ota.cpp).

void deviceOnly(AsyncWebServerRequest* request, ArMiddlewareNext next) {
  const AsyncWebHeader* origin = request->getHeader("Origin");
  const char* refusal = refuseRequest(origin != nullptr ? origin->value().c_str() : nullptr,
                                      request->host().c_str(), app->hostname());
  if (refusal == nullptr) {
    next();
    return;
  }
  LOG_W(kTag, "%s refused: %s (origin %s, host %s)", request->url().c_str(), refusal,
        origin != nullptr ? origin->value().c_str() : "-", request->host().c_str());
  if (request->url().startsWith("/api/") || request->url() == "/ws") {
    sendError(request, 403, refusal);
    return;
  }
  // A page opened under another name: say how to reach the device.
  char text[192];
  std::snprintf(text, sizeof(text),
                "403: %s.\nOpen the device by its IP address or as http://%s.local/\n", refusal,
                app->hostname());
  request->send(403, "text/plain; charset=utf-8", text);
}

void notFound(AsyncWebServerRequest* request) {
  if (request->url().startsWith("/api/")) {
    sendError(request, 404, "not found");
    return;
  }
  request->send(404, "text/plain; charset=utf-8", "not found");
}

}  // namespace

void WebServer::begin(App& target, WsHub& hub, Ota& ota) {
  app = &target;
  server.addMiddleware(captivePortal);   // Runs before every handler.
  server.addMiddleware(deviceOnly);      // After the redirect of foreign hosts in AP mode.
  hub.attach(server, target);
  ota.attach(server, target);
  using M = AsyncURIMatcher;
  serveAssets();

  server.on(M::exact("/api/state"), HTTP_GET, getState);
  server.on(M::exact("/api/state"), HTTP_POST, postState, nullptr, collectBody(kStateBodyMax));
  server.on(M::exact("/api/effects"), HTTP_GET, getEffects);
  server.on(M::exact("/api/palettes"), HTTP_GET, getPalettes);
  server.on(M::exact("/api/paint"), HTTP_GET, getPaint);
  server.on(M::exact("/api/config"), HTTP_GET, getConfig);
  server.on(M::exact("/api/config"), HTTP_POST, postConfig, nullptr, collectBody(kConfigBodyMax));
  server.on(M::exact("/api/presets"), HTTP_GET, getPresets);
  server.on(M::exact("/api/presets"), HTTP_POST, postPreset, nullptr, collectBody(kPresetBodyMax));
  server.on(M::prefix("/api/presets/"), HTTP_POST | HTTP_DELETE, presetItem, nullptr,
            collectBody(kPresetBodyMax));
  server.on(M::exact("/api/wifi/scan"), HTTP_POST, postScan);
  server.on(M::exact("/api/wifi/scan"), HTTP_GET, getScan);
  server.on(M::exact("/api/info"), HTTP_GET, getInfo);
  server.on(M::exact("/api/restart"), HTTP_POST, postRestart);
  server.on(M::exact("/api/test-pattern"), HTTP_POST, postTestPattern, nullptr,
            collectBody(kPresetBodyMax));
  server.onNotFound(notFound);
  server.begin();
  LOG_I(kTag, "HTTP server on port 80");
}

}  // namespace ledframe

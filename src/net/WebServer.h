// HTTP server with the REST API (ESP32 only).
//
// Handlers run in the AsyncTCP task. They never touch the engine: they parse the
// body with ApiJson, queue commands through App and answer right away. Reads come
// from App::snapshot() and the other thread-safe App methods. docs/API.md
// describes every endpoint.
#pragma once

namespace ledframe {

class App;
class Ota;
class WsHub;

class WebServer {
 public:
  // Registers the routes, the WebSocket hub and POST /api/ota, then listens on
  // port 80. Call after Wi-Fi started.
  void begin(App& app, WsHub& hub, Ota& ota);
};

}  // namespace ledframe

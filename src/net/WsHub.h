// WebSocket hub on /ws (ESP32 only).
//
// - Text messages from clients are partial state changes, parsed with
//   parseStatePatch() exactly like POST /api/state. Errors go back to the sender
//   as {"error": "..."}; a successful change shows up in the next broadcast.
// - The state JSON of GET /api/state is broadcast to all clients after every
//   change of the snapshot version, at most every 50 ms. A new client gets it
//   right after connecting.
// - At most kMaxClients clients; a further client is closed with code 1013.
// - Binary messages are dispatched by their first byte (WsProtocol.h):
//   - paint changes go to App and, once accepted, to every other client, so all
//     open paint editors show the same picture;
//   - live preview: subscribed clients get the shown canvas when it changes, at
//     most about 15 frames per second and one frame in flight per client.
#pragma once

#include <cstdint>

class AsyncWebServer;

namespace ledframe {

class App;

class WsHub {
 public:
  static constexpr uint8_t kMaxClients = 4;

  // Registers /ws on the server. Call before server.begin().
  void attach(AsyncWebServer& server, App& app);
  // Arduino loop task: broadcasts state changes and frees closed clients.
  void tick(uint32_t nowMs);
};

}  // namespace ledframe

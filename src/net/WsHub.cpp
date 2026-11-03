#include "net/WsHub.h"

#include <ESPAsyncWebServer.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "app/App.h"
#include "core/Log.h"
#include "effects/EffectRegistry.h"
#include "net/ApiJson.h"
#include "net/WsProtocol.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "ws";
constexpr size_t kMessageMax = 2048;  // text: same limit as POST /api/state
constexpr size_t kTextMax = kMessageMax;
constexpr uint32_t kAssemblyTimeoutMs = 2000;  // a half-received message blocks others no longer
constexpr size_t kStateJsonMax = 1024;
constexpr uint16_t kTryAgainLater = 1013;  // WebSocket close code
constexpr uint32_t kCleanupIntervalMs = 1000;

AsyncWebSocket socket("/ws");
App* app = nullptr;
BroadcastThrottle throttle;
uint32_t lastCleanupMs = 0;

// --- Clients and preview subscriptions ------------------------------------------
// Changed by WebSocket events (AsyncTCP task) and read by tick() (Arduino loop
// task) under slotsLock. Never call the socket while holding slotsLock: the library
// calls onEvent under its own lock, so the other order could deadlock.

struct ClientSlot {
  uint32_t id = 0;  // 0: free
  bool preview = false;
  uint32_t sentVersion = 0;          // preview frame version last sent
  AsyncWebSocketSharedBuffer frame;  // reused once the library has sent it
};

constexpr size_t kSlots = WsHub::kMaxClients;
ClientSlot slots[kSlots];
std::mutex slotsLock;

void addClient(uint32_t id) {
  std::lock_guard<std::mutex> lock(slotsLock);
  for (ClientSlot& slot : slots) {
    if (slot.id == 0) {
      slot = ClientSlot{};
      slot.id = id;
      return;
    }
  }
}

// Removes the client or changes its preview subscription; tells App whether any
// client still wants the preview.
void updateClient(uint32_t id, bool remove, bool preview) {
  bool anyPreview = false;
  {
    std::lock_guard<std::mutex> lock(slotsLock);
    for (ClientSlot& slot : slots) {
      if (slot.id == id) {
        if (remove) {
          slot = ClientSlot{};  // The library keeps a frame it still sends.
        } else {
          slot.preview = preview;
          slot.sentVersion = 0;  // A new subscriber gets the current frame.
        }
      }
      anyPreview |= slot.id != 0 && slot.preview;
    }
  }
  app->setPreviewWanted(anyPreview);
}

// Sends the latest preview frame to subscribers that do not have it. A client
// whose previous frame is still queued (the library holds its buffer) is skipped
// and gets the newest frame later, so a slow client never piles up frames.
// App copies the frame straight into each client's buffer. Taking App's mutex
// under slotsLock is safe: the render task never takes slotsLock.
void sendPreview() {
  struct Target {
    uint32_t id;
    AsyncWebSocketSharedBuffer frame;
  };
  Target targets[kSlots];
  size_t count = 0;
  {
    std::lock_guard<std::mutex> lock(slotsLock);
    for (ClientSlot& slot : slots) {
      if (slot.id == 0 || !slot.preview) {
        continue;
      }
      if (!slot.frame) {
        slot.frame = std::make_shared<std::vector<uint8_t>>();
      } else if (slot.frame.use_count() > 1) {
        continue;
      }
      if (app->copyPreview(slot.sentVersion, *slot.frame) == 0) {
        continue;  // The client has the newest frame.
      }
      targets[count++] = Target{slot.id, slot.frame};
    }
  }
  for (size_t i = 0; i < count; ++i) {
    socket.binary(targets[i].id, targets[i].frame);
  }
}

// An accepted paint message goes to every other client as it is.
void forwardPaint(uint32_t sender, const uint8_t* data, size_t length) {
  uint32_t ids[kSlots];
  size_t count = 0;
  {
    std::lock_guard<std::mutex> lock(slotsLock);
    for (const ClientSlot& slot : slots) {
      if (slot.id != 0 && slot.id != sender) {
        ids[count++] = slot.id;
      }
    }
  }
  if (count == 0) {
    return;
  }
  const AsyncWebSocketSharedBuffer buffer =
      std::make_shared<std::vector<uint8_t>>(data, data + length);
  for (size_t i = 0; i < count; ++i) {
    socket.binary(ids[i], buffer);
  }
}

// --- Messages ------------------------------------------------------------------

void sendError(AsyncWebSocketClient* client, const char* message) {
  char buffer[128];
  const size_t length = writeError(message, buffer, sizeof(buffer));
  if (length > 0) {
    client->text(buffer, length);
  }
}

void sendState(AsyncWebSocketClient* client) {
  const StateSnapshot state = app->snapshot();
  char buffer[kStateJsonMax];
  const size_t length = writeState(state, EffectRegistry::global(), buffer, sizeof(buffer));
  if (length > 0) {
    client->text(buffer, length);
  }
}

void handleText(AsyncWebSocketClient* client, const uint8_t* data, size_t length) {
  if (isPing(data, length)) {
    client->text(kPongText);
    return;
  }
  if (length > kTextMax) {
    sendError(client, "message too large");
    return;
  }
  const StateSnapshot state = app->snapshot();
  CommandBatch batch;
  ApiError error;
  IgnoredFields ignored;
  if (!parseStatePatch(reinterpret_cast<const char*>(data), length, EffectRegistry::global(),
                       state.effect, batch, error, &ignored)) {
    sendError(client, error.message);
    return;
  }
  for (uint8_t i = 0; i < ignored.count && i < IgnoredFields::kMaxNames; ++i) {
    LOG_W(kTag, "client %lu: unknown field '%s' ignored", static_cast<unsigned long>(client->id()),
          ignored.names[i]);
  }
  for (uint8_t i = 0; i < batch.count; ++i) {
    if (!app->post(batch.commands[i])) {
      sendError(client, "command queue full");
      return;
    }
  }
}

// WebSocket events run one at a time in the AsyncTCP task, so one buffer is enough
// and keeps about 3 KB off its stack.
PaintOp paintOps[kPaintMaxPixels];

// Pixels outside the picture are dropped and reported; the rest are applied.
void handlePaint(AsyncWebSocketClient* client, const uint8_t* data, size_t length) {
  const StateSnapshot state = app->snapshot();
  const PaintParse parsed = parsePaintMessage(data, length, state.layout.width,
                                              state.layout.height, paintOps, kPaintMaxPixels);
  if (parsed.error != nullptr) {
    sendError(client, parsed.error);
    return;
  }
  if (parsed.count > 0 && !app->postPaint(paintOps, parsed.count)) {
    sendError(client, "paint queue full, send again");
    return;
  }
  if (parsed.count > 0) {
    forwardPaint(client->id(), data, length);
  }
  if (parsed.outside > 0) {
    char message[64];
    std::snprintf(message, sizeof(message), "%u paint pixels outside the picture",
                  static_cast<unsigned>(parsed.outside));
    sendError(client, message);
  }
}

void handleBinary(AsyncWebSocketClient* client, const uint8_t* data, size_t length) {
  switch (classifyBinary(data, length)) {
    case BinaryKind::kPaintPixels:
      handlePaint(client, data, length);
      return;
    case BinaryKind::kPreview:
      switch (parsePreviewRequest(data, length)) {
        case PreviewRequest::kStart:
          updateClient(client->id(), false, true);
          return;
        case PreviewRequest::kStop:
          updateClient(client->id(), false, false);
          return;
        case PreviewRequest::kInvalid:
          break;
      }
      sendError(client, "preview takes 0x02 0x01 (start) or 0x02 0x00 (stop)");
      return;
    case BinaryKind::kEmpty:
    case BinaryKind::kUnknown:
      sendError(client, "unknown binary message");
      return;
  }
}

void dispatch(AsyncWebSocketClient* client, uint8_t opcode, const uint8_t* data, size_t length) {
  if (opcode == WS_TEXT) {
    handleText(client, data, length);
  } else if (opcode == WS_BINARY) {
    handleBinary(client, data, length);
  }
}

// A frame longer than one TCP segment (about 1.4 KB) arrives in parts. They are
// collected here, one message at a time: the UI sends large messages only while
// drawing. A message split into several WebSocket frames is not supported.
struct Assembly {
  bool active = false;
  uint32_t client = 0;
  uint32_t startedMs = 0;
  size_t length = 0;
  uint8_t data[kMessageMax];
};
Assembly assembly;

void onData(AsyncWebSocketClient* client, const AwsFrameInfo& info, const uint8_t* data,
            size_t length) {
  if (!info.final || info.num != 0) {
    if (info.index == 0) {
      sendError(client, "fragmented messages are not supported");
    }
    return;
  }
  if (info.len > kMessageMax) {
    if (info.index == 0) {
      sendError(client, "message too large");
    }
    return;
  }
  if (info.index == 0 && length == info.len) {
    dispatch(client, info.opcode, data, length);  // The usual case: one part.
    return;
  }
  if (info.index == 0) {
    const bool othersBusy = assembly.active && assembly.client != client->id() &&
                            millis() - assembly.startedMs < kAssemblyTimeoutMs;
    if (othersBusy) {
      sendError(client, "busy, send again");
      return;
    }
    assembly.active = true;
    assembly.client = client->id();
    assembly.startedMs = millis();
    assembly.length = 0;
  } else if (!assembly.active || assembly.client != client->id() ||
             info.index != assembly.length) {
    return;  // Part of a message that was refused or lost.
  }
  std::memcpy(assembly.data + assembly.length, data, length);
  assembly.length += length;
  if (assembly.length == info.len) {
    assembly.active = false;
    dispatch(client, info.opcode, assembly.data, assembly.length);
  }
}

void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg,
             uint8_t* data, size_t length) {
  switch (type) {
    case WS_EVT_CONNECT:
      if (server->count() > WsHub::kMaxClients) {
        LOG_W(kTag, "client %lu rejected: %u clients max", static_cast<unsigned long>(client->id()),
              static_cast<unsigned>(WsHub::kMaxClients));
        client->close(kTryAgainLater, "too many clients");
        return;
      }
      LOG_I(kTag, "client %lu connected (%u)", static_cast<unsigned long>(client->id()),
            static_cast<unsigned>(server->count()));
      addClient(client->id());
      sendState(client);
      return;
    case WS_EVT_DISCONNECT:
      LOG_I(kTag, "client %lu disconnected", static_cast<unsigned long>(client->id()));
      if (assembly.client == client->id()) {
        assembly.active = false;
      }
      updateClient(client->id(), true, false);
      return;
    case WS_EVT_DATA:
      onData(client, *static_cast<const AwsFrameInfo*>(arg), data, length);
      return;
    case WS_EVT_PING:
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      return;
  }
}

}  // namespace

void WsHub::attach(AsyncWebServer& server, App& target) {
  app = &target;
  socket.onEvent(onEvent);
  server.addHandler(&socket);
}

void WsHub::tick(uint32_t nowMs) {
  if (app == nullptr) {
    return;
  }
  if (nowMs - lastCleanupMs >= kCleanupIntervalMs) {
    lastCleanupMs = nowMs;
    socket.cleanupClients(kMaxClients);
  }
  if (socket.count() == 0) {
    return;
  }
  sendPreview();
  const StateSnapshot state = app->snapshot();
  if (!throttle.shouldSend(state.version, nowMs)) {
    return;
  }
  char buffer[kStateJsonMax];
  const size_t length = writeState(state, EffectRegistry::global(), buffer, sizeof(buffer));
  if (length > 0) {
    socket.textAll(buffer, length);
  }
  throttle.markSent(state.version, nowMs);
}

}  // namespace ledframe

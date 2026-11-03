// Device API: REST requests and the /ws WebSocket (docs/API.md).
//
// Device keeps one WebSocket open and reconnects on its own. State changes go out
// through it, merged and limited to one message per SEND_INTERVAL_MS, so a dragged
// slider sends about 20 updates per second. Without a WebSocket (connecting, or the
// device has its 4 clients already) changes go through POST /api/state and the
// state is polled instead.
//
// A restarted or unplugged device does not close the socket, so the page sends the
// text "ping" every PING_MS (and when it comes back to the foreground). The device
// answers "pong"; with no message at all for PONG_TIMEOUT_MS the socket is dropped
// and a new one is opened. After a device restart the ping also makes the old
// socket fail at once.

const SEND_INTERVAL_MS = 50;
const RETRY_MIN_MS = 1000;
const RETRY_MAX_MS = 10000;
const RETRY_BUSY_MS = 15000;  // after close code 1013: too many clients
const POLL_MS = 3000;
const PING_MS = 10000;
const PONG_TIMEOUT_MS = 5000;

async function check(response) {
  if (response.ok) {
    return response.json();
  }
  let message = `HTTP ${response.status}`;
  try {
    message = (await response.json()).error || message;
  } catch {
    // Not JSON: keep the status.
  }
  throw new Error(message);
}

export function getJson(path) {
  return fetch(path, { cache: "no-store" }).then(check);
}

export function requestJson(method, path, body) {
  const options = { method };
  if (body !== undefined) {
    options.headers = { "Content-Type": "application/json" };
    options.body = JSON.stringify(body);
  }
  return fetch(path, options).then(check);
}

export function postJson(path, body) {
  return requestJson("POST", path, body);
}

// Merges a state patch into pending changes; params are merged by id.
function merge(target, patch) {
  for (const [key, value] of Object.entries(patch)) {
    if (key === "params" && target.params) {
      Object.assign(target.params, value);
    } else {
      target[key] = key === "params" ? { ...value } : value;
    }
  }
  return target;
}

export class Device {
  // handlers: onState(state), onLink(status, detail), onError(message)
  constructor(handlers) {
    this.handlers = handlers;
    this.ws = null;
    this.retryMs = RETRY_MIN_MS;
    this.retryTimer = 0;
    this.pollTimer = 0;
    this.pending = null;
    this.sendTimer = 0;
    this.lastSend = 0;
    this.pingTimer = 0;
    this.pongTimer = 0;
    this.listeners = { binary: new Set(), open: new Set() };
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) {
        return;
      }
      // Phones freeze or drop sockets in the background: check as soon as the page
      // is back.
      if (this.isOpen()) {
        this.ping();
      } else {
        this.connect();
      }
    });
  }

  isOpen() {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN;
  }

  // Listeners for "binary" (fn(Uint8Array) for every binary message) and "open"
  // (fn() after every (re)connect, e.g. to subscribe again). Returns a remover.
  on(event, fn) {
    this.listeners[event].add(fn);
    return () => this.listeners[event].delete(fn);
  }

  emit(event, value) {
    for (const fn of this.listeners[event]) {
      try {
        fn(value);
      } catch (error) {
        console.warn(`${event} listener:`, error);
      }
    }
  }

  connect() {
    clearTimeout(this.retryTimer);
    this.stopHeartbeat();
    if (this.ws) {
      this.ws.onclose = null;
      this.ws.close();
    }
    this.handlers.onLink("connecting");
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = "arraybuffer";
    this.ws = ws;
    ws.onopen = () => {
      this.retryMs = RETRY_MIN_MS;
      this.stopPolling();
      this.handlers.onLink("online");
      this.pingTimer = setInterval(() => this.ping(), PING_MS);
      this.emit("open");
    };
    ws.onmessage = (event) => {
      clearTimeout(this.pongTimer);  // Any message shows the link is alive.
      this.pongTimer = 0;
      if (typeof event.data !== "string") {
        this.emit("binary", new Uint8Array(event.data));  // paint changes, preview frames
        return;
      }
      if (event.data === "pong") {
        return;
      }
      let message;
      try {
        message = JSON.parse(event.data);
      } catch {
        return;
      }
      if (message.error) {
        this.handlers.onError(message.error);
      } else {
        this.handlers.onState(message);  // The device sends the state on connect.
      }
    };
    ws.onclose = (event) => this.closed(event.code === 1013);
  }

  // The socket is gone: retry later and poll the state meanwhile.
  closed(busy) {
    this.stopHeartbeat();
    this.ws = null;
    this.handlers.onLink("offline", busy ? "Too many open pages" : "Reconnecting");
    this.startPolling();
    clearTimeout(this.retryTimer);
    this.retryTimer = setTimeout(() => this.connect(), busy ? RETRY_BUSY_MS : this.retryMs);
    this.retryMs = Math.min(this.retryMs * 2, RETRY_MAX_MS);
  }

  ping() {
    if (!this.isOpen() || this.pongTimer) {
      return;
    }
    this.ws.send("ping");
    this.pongTimer = setTimeout(() => {
      // No answer: a dead connection. close() alone could wait for TCP to time out.
      if (this.ws) {
        this.ws.onclose = null;
        this.ws.close();
      }
      this.closed(false);
    }, PONG_TIMEOUT_MS);
  }

  stopHeartbeat() {
    clearInterval(this.pingTimer);
    clearTimeout(this.pongTimer);
    this.pingTimer = 0;
    this.pongTimer = 0;
  }

  // Queues a state patch. Changes within SEND_INTERVAL_MS are merged; now = true
  // sends at once (button presses), together with anything still queued.
  send(patch, { now = false } = {}) {
    this.pending = merge(this.pending || {}, patch);
    const wait = this.lastSend + SEND_INTERVAL_MS - Date.now();
    if (now || wait <= 0) {
      this.flush();
    } else if (!this.sendTimer) {
      this.sendTimer = setTimeout(() => this.flush(), wait);
    }
  }

  flush() {
    clearTimeout(this.sendTimer);
    this.sendTimer = 0;
    const patch = this.pending;
    this.pending = null;
    if (!patch) {
      return;
    }
    this.lastSend = Date.now();
    if (this.isOpen()) {
      this.ws.send(JSON.stringify(patch));
      return;
    }
    postJson("/api/state", patch)
      .then(() => this.refresh())
      .catch((error) => this.handlers.onError(error.message));
  }

  // Binary message for widgets; false when there is no WebSocket.
  sendBinary(data) {
    if (!this.isOpen()) {
      return false;
    }
    this.ws.send(data);
    return true;
  }

  refresh() {
    return getJson("/api/state")
      .then((state) => this.handlers.onState(state))
      .catch(() => {});
  }

  startPolling() {
    if (!this.pollTimer) {
      this.pollTimer = setInterval(() => this.refresh(), POLL_MS);
    }
  }

  stopPolling() {
    clearInterval(this.pollTimer);
    this.pollTimer = 0;
  }
}

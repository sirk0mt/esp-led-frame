// Status bar at the bottom of every page: firmware, local time, Wi-Fi, frame rate and
// uptime in one line; opened, the full GET /api/info. Shared with the device settings
// (onInfo), so the page asks the device for its information in one place only.
//
// /api/info is fetched every INFO_MS while the page is visible. Fewer requests also
// mean less Wi-Fi traffic, which can disturb the LED signal. Between fetches the
// clock in the bar runs on in the browser.

import { h } from "./controls.js";

const INFO_MS = 10000;

export function formatUptime(seconds) {
  const d = Math.floor(seconds / 86400);
  const hr = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return d > 0 ? `${d} d ${hr} h` : hr > 0 ? `${hr} h ${m} min` : `${m} min ${seconds % 60} s`;
}

// Short Wi-Fi state for the one-line bar; the open list has the network name.
function wifiText(w) {
  return { connected: `${w.rssi} dBm`, connecting: "connecting", ap: "access point", off: "off" }[w.mode] || w.mode;
}

const pad = (n) => String(n).padStart(2, "0");

// footer: the element to fill. Returns { current(), refresh(), onInfo(fn), stop() };
// onInfo returns a function that removes the listener.
export function startStatus(footer, { getJson }) {
  const line = h("span", { class: "status-line" }, "Loading…");
  const list = h("dl", { class: "info" });
  const details = h("details", {}, h("summary", {}, line), list);
  footer.replaceChildren(details);

  let info = null;
  let clockBase = null;  // { ms: device local time as UTC ms, at: performance.now() }
  const listeners = new Set();

  const localNow = () => (clockBase ? new Date(clockBase.ms + performance.now() - clockBase.at) : null);
  const timeText = (seconds) => {
    const t = localNow();
    if (!t) {
      return "time not synchronized";
    }
    const hm = `${pad(t.getUTCHours())}:${pad(t.getUTCMinutes())}`;
    return seconds ? `${t.getUTCFullYear()}-${pad(t.getUTCMonth() + 1)}-${pad(t.getUTCDate())} ${hm}:` +
                     pad(t.getUTCSeconds()) : hm;
  };

  const showLine = () => {
    if (!info) {
      return;
    }
    const e = info.engine;
    line.textContent = [`v${info.version}`, timeText(false), `Wi-Fi ${wifiText(info.wifi)}`,
                        `${e.fps} fps`, `up ${formatUptime(info.uptime)}`].join(" · ");
    line.dataset.kind = info.wifi.mode === "connected" ? "" : "warn";
  };

  const showList = () => {
    const e = info.engine;
    const w = info.wifi;
    const rows = [
      ["Firmware", `${info.version} (built ${info.buildDate})`],
      ["Time", info.time && info.time.synced
        ? `${timeText(true)}${info.time.sntp ? "" : " (not synchronizing now)"}`
        : info.time && info.time.sntp ? "waiting for the time server" : "not synchronized (no internet)"],
      ["Wi-Fi", w.mode === "connected" ? `${w.ssid}, ${w.ip}, signal ${w.rssi} dBm`
        : w.mode === "ap" ? `access point ${w.apName} at ${w.ip}, ${w.apClients} connected`
        : w.mode === "connecting" ? `connecting to ${w.ssid}` : wifiText(w)],
      ["Uptime", formatUptime(info.uptime)],
      ["Free memory", `${Math.round(info.freeHeap / 1024)} KB (lowest ${Math.round(info.minFreeHeap / 1024)} KB)`],
      ["LEDs", `${info.leds} of ${info.maxLeds}`],
      ["Frames", `${e.fps} fps of ${e.targetFps}, ${e.shownFps} sent to the LEDs`],
      ["Render time", `${e.renderUsAvg} µs average, ${e.renderUsMax} µs max`],
      ["LED output", `${Math.round(e.showUsAvg / 100) / 10} ms per frame`],
      ["Last restart", info.resetReason || "unknown"],
    ];
    if (info.lastCrash) {
      rows.push(["Last crash", `task ${info.lastCrash.task} at ${info.lastCrash.pc}; ` +
                                `backtrace ${info.lastCrash.backtrace.join(" ")}`]);
    }
    list.replaceChildren(...rows.flatMap(([k, v]) => [h("dt", {}, k), h("dd", {}, v)]));
  };

  const refresh = async () => {
    try {
      info = await getJson("/api/info");
    } catch {
      line.textContent = info ? `${line.textContent.split(" · ")[0]} · offline` : "Device offline";
      line.dataset.kind = "warn";
      return;
    }
    const local = info.time && info.time.synced && info.time.local;
    // "YYYY-MM-DD HH:MM:SS" read as UTC: only the fields matter, not the browser zone.
    clockBase = local ? { ms: Date.parse(local.replace(" ", "T") + "Z"), at: performance.now() } : null;
    showLine();
    showList();
    listeners.forEach((fn) => fn(info));
  };

  const poll = setInterval(() => {
    if (!document.hidden) {
      refresh();
    }
  }, INFO_MS);
  // The clock in the bar ticks every second; the open list shows seconds.
  const tick = setInterval(() => {
    if (!document.hidden && info) {
      showLine();
      if (details.open) {
        showList();
      }
    }
  }, 1000);
  const onVisible = () => {
    if (!document.hidden) {
      refresh();
    }
  };
  document.addEventListener("visibilitychange", onVisible);
  refresh();

  return {
    current: () => info,
    refresh,
    onInfo(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    stop() {
      clearInterval(poll);
      clearInterval(tick);
      document.removeEventListener("visibilitychange", onVisible);
    },
  };
}

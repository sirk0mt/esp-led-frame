// Entry point of the web UI: top bar with the page tabs (Effects, Presets, Device),
// effect tiles, parameter panel and widgets, kept in sync with the device state from
// the WebSocket, and the status bar at the bottom.

// Static imports are kept to two modules: the device accepts only a few connections
// at once, and more parallel requests stall the page load by a second.
import { Device, getJson, postJson, requestJson } from "./api.js";
import {
  briPercent, briToPosition, held, hold, positionToBri, renderEffects, renderParams,
} from "./ui/controls.js";

const $ = (id) => document.getElementById(id);

const ui = {
  link: $("link"),
  linkText: $("link-text"),
  power: $("power"),
  bri: $("bri"),
  briValue: $("bri-value"),
  effects: $("effects"),
  settings: $("settings"),
  params: $("params"),
  widget: $("widget"),
  notice: $("notice"),
  toast: $("toast"),
  preview: $("preview"),
  previewToggle: $("preview-toggle"),
  presets: $("presets"),
  device: $("device"),
  status: $("status"),
};

let effects = [];
let palettes = [];
let tiles = null;
let state = null;
let shownEffect = null;  // effect id the panel is built for
let panel = null;
let widget = null;
let widgetToken = 0;
let wasOnline = false;
// Effect chosen here that the device has not confirmed yet. States with another
// effect are skipped for up to WANTED_MS; after that the device state wins.
const WANTED_MS = 2000;
let wanted = null;

const device = new Device({ onState: applyState, onLink: showLink, onError: toast });
const send = (patch, now) => device.send(patch, { now });

// --- Device metadata ---------------------------------------------------------

async function loadMetadata() {
  const [nextEffects, nextPalettes] = await Promise.all([
    getJson("/api/effects"),
    getJson("/api/palettes"),
  ]);
  const changed =
    JSON.stringify(nextEffects) !== JSON.stringify(effects) ||
    JSON.stringify(nextPalettes) !== JSON.stringify(palettes);
  effects = nextEffects;
  palettes = nextPalettes;
  if (changed) {
    tiles = renderEffects(ui.effects, effects, selectEffect);
    shownEffect = null;  // Rebuild the panel with the new schema.
    if (state) {
      applyState(state);
    }
  }
}

async function start() {
  for (let delay = 1000; ; delay = Math.min(delay * 2, 10000)) {
    try {
      await loadMetadata();
      ui.notice.hidden = true;
      break;
    } catch {
      ui.notice.textContent = "Cannot reach the device. Retrying…";
      ui.notice.hidden = false;
      await new Promise((resolve) => setTimeout(resolve, delay));
    }
  }
  device.connect();
  if (previewWanted()) {
    setPreview(true).catch(() => toast("Cannot load the preview"));
  }
  // Presets and the status bar load after the first requests and one after the
  // other, not in parallel.
  try {
    const { mountPresets } = await import("./ui/presets.js");
    await mountPresets(ui.presets, { getJson, requestJson, device, effects: () => effects });
  } catch {
    ui.presets.textContent = "Cannot load the presets.";
  }
  try {
    const { startStatus } = await import("./ui/status.js");
    status = startStatus(ui.status, { getJson });
  } catch {
    ui.status.textContent = "Cannot load the device information.";
  }
  if (shownPage === "device") {
    openDevice(pageFromHash().group);  // The device page waited for the status bar.
  }
}

// --- State -------------------------------------------------------------------

function applyState(next) {
  state = next;
  if (!held("on")) {
    ui.power.setAttribute("aria-pressed", String(next.on));
  }
  if (!held("bri")) {
    ui.bri.value = briToPosition(next.bri);
    showBrightness(next.bri);
  }
  if (!tiles) {
    return;
  }
  if (wanted && next.effect !== wanted.id && Date.now() < wanted.until) {
    return;  // An older state: it would show the previous effect again.
  }
  wanted = null;
  tiles.setActive(next.effect);
  if (next.effect !== shownEffect) {
    showEffect(next.effect);
  }
  panel?.update(next);
  widget?.update(next);
}

function showEffect(id) {
  shownEffect = id;
  widget?.destroy();
  widget = null;
  ui.widget.replaceChildren();
  const token = ++widgetToken;  // A widget still loading for the old effect is dropped.
  const effect = effects.find((e) => e.id === id);
  if (!effect) {
    ui.settings.hidden = true;
    panel = null;
    return;
  }
  panel = renderParams(ui.params, effect, palettes, send);
  ui.settings.hidden = effect.params.length === 0 && !effect.widget && !effect.usesPalette;
  if (!effect.widget) {
    return;
  }
  const ctx = {
    effect,
    state,
    send,
    sendBinary: (data) => device.sendBinary(data),
    on: (event, fn) => device.on(event, fn),
    getJson,
    postJson,
  };
  // Each widget gets its own element, so a late one from an older choice cannot
  // touch the current widget.
  const host = document.createElement("div");
  ui.widget.append(host);
  import("./ui/widgets.js")
    .then(({ mountWidget }) => mountWidget(effect.widget, host, ctx))
    .then((mounted) => {
      if (token !== widgetToken) {
        mounted.destroy();  // Another effect was chosen while loading.
        return;
      }
      widget = mounted;
      widget.update(state);
    })
    .catch(() => toast("Cannot load the effect editor"));
}

function selectEffect(id) {
  if (id === state?.effect && !wanted) {
    return;
  }
  const choice = { id, until: Date.now() + WANTED_MS };
  wanted = choice;
  tiles.setActive(id);
  send({ effect: id }, true);
  // No confirmation (e.g. the request failed): show the device state again.
  setTimeout(() => {
    if (wanted === choice) {
      wanted = null;
      if (state) {
        applyState(state);
      }
    }
  }, WANTED_MS + 50);
}

// --- Live preview --------------------------------------------------------------
// Off by default; the choice is remembered in this browser. The module loads only
// when the preview is turned on.

const PREVIEW_KEY = "ledframe.preview";
let preview = null;

async function setPreview(on) {
  ui.previewToggle.checked = on;
  ui.preview.hidden = !on;
  try {
    localStorage.setItem(PREVIEW_KEY, on ? "1" : "0");
  } catch {
    // Private mode or blocked storage: the switch still works for this visit.
  }
  if (on && !preview) {
    const { startPreview } = await import("./ui/preview.js");
    if (ui.previewToggle.checked && !preview) {
      preview = startPreview(ui.preview, device);
    }
  } else if (!on && preview) {
    preview.stop();
    preview = null;
  }
}

ui.previewToggle.addEventListener("change", () => {
  setPreview(ui.previewToggle.checked).catch(() => toast("Cannot load the preview"));
});

function previewWanted() {
  try {
    return localStorage.getItem(PREVIEW_KEY) === "1";
  } catch {
    return false;
  }
}

// --- Pages ------------------------------------------------------------------------
// One page at a time, chosen by the address: #effects (default), #presets, #device,
// and #device/<group> for a settings group. The back button and a reload keep it.

const PAGES = ["effects", "presets", "device"];
let status = null;  // the status bar (ui/status.js), once loaded
let shownPage = null;

function pageFromHash() {
  const [page, group] = location.hash.slice(1).split("/");
  return { page: PAGES.includes(page) ? page : "effects", group };
}

function showPage() {
  const { page, group } = pageFromHash();
  for (const name of PAGES) {
    $(`page-${name}`).hidden = name !== page;
    $(`tab-${name}`).setAttribute("aria-selected", String(name === page));
  }
  if (page !== shownPage) {
    shownPage = page;
    window.scrollTo(0, 0);
    if (page === "device") {
      openDevice(group);
    } else {
      closeDevice();
    }
  } else if (page === "device") {
    deviceSettings?.showGroup(group);
  }
}

window.addEventListener("hashchange", showPage);

// --- Device settings ------------------------------------------------------------
// Loaded on first open and built again on every open, with fresh values.

let deviceSettings = null;
let deviceToken = 0;

function closeDevice() {
  ++deviceToken;
  deviceSettings?.destroy();
  deviceSettings = null;
  ui.device.replaceChildren();
}

async function openDevice(group) {
  closeDevice();
  const token = deviceToken;
  if (!status) {
    ui.device.replaceChildren();
    ui.device.textContent = "Loading…";  // start() shows the page again when ready
    return;
  }
  try {
    const { mountSettings } = await import("./ui/settings.js");
    const mounted = await mountSettings(ui.device, { getJson, postJson, device, status, group });
    if (token !== deviceToken) {
      mounted.destroy();  // Another page was chosen while loading.
      return;
    }
    deviceSettings = mounted;
  } catch (error) {
    ui.device.textContent = `Cannot load the settings: ${error.message}`;
  }
}

// --- Top bar -----------------------------------------------------------------

function showBrightness(bri) {
  ui.briValue.value = briPercent(bri);
}

ui.power.addEventListener("click", () => {
  const on = ui.power.getAttribute("aria-pressed") !== "true";
  hold("on");
  ui.power.setAttribute("aria-pressed", String(on));
  send({ on }, true);
});

ui.bri.addEventListener("input", () => {
  const bri = positionToBri(Number(ui.bri.value));
  hold("bri");
  showBrightness(bri);
  send({ bri }, false);
});

ui.bri.addEventListener("change", () => {
  hold("bri");
  send({ bri: positionToBri(Number(ui.bri.value)) }, true);
});

function showLink(status, detail) {
  ui.link.dataset.state = status;
  const text = { connecting: "Connecting", online: "Live", offline: detail || "Offline" }[status];
  ui.linkText.textContent = text;
  ui.link.title = text;
  if (status === "online") {
    if (wasOnline) {
      // The device may have restarted with new firmware: reload the effect list.
      loadMetadata().catch(() => {});
    }
    wasOnline = true;
  }
}

let toastTimer = 0;
function toast(message) {
  ui.toast.textContent = message;
  ui.toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (ui.toast.hidden = true), 4000);
}

showPage();
start();

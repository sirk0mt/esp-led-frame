// Device settings and system information.
// Loaded when the "Device" section is opened for the first time.
//
// Groups, one at a time behind tabs: LED (layout, data pin, color order with the test
// pattern, power limit, FPS), network (status, scan, save), system (hostname, OTA password,
// time zone, restart), on/off schedule and firmware update with a progress
// bar. GET /api/info comes from the status bar (ui/status.js), which also shows it.

import { h } from "./controls.js";

const ORIGINS = ["top-left", "top-right", "bottom-left", "bottom-right"];
const ROTATIONS = [0, 90, 180, 270];
const COLOR_ORDERS = ["RGB", "RBG", "GRB", "GBR", "BRG", "BGR"];
const HOSTNAME = /^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$/;
// POSIX TZ rules: the same check as isValidTimezone() in the firmware.
const TIMEZONE = /^[A-Za-z<][A-Za-z0-9+\-,./:<>]{0,62}$/;
const TIMEZONES = [
  ["CET-1CEST,M3.5.0,M10.5.0/3", "Central Europe (Warsaw, Berlin, Paris)"],
  ["GMT0BST,M3.5.0/1,M10.5.0", "UK and Ireland (London)"],
  ["EET-2EEST,M3.5.0/3,M10.5.0/4", "Eastern Europe (Helsinki, Kyiv)"],
  ["UTC0", "UTC"],
  ["EST5EDT,M3.2.0,M11.1.0", "US Eastern (New York)"],
  ["PST8PDT,M3.2.0,M11.1.0", "US Pacific (Los Angeles)"],
];
const CUSTOM = "custom";
const DAYS = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];  // schedule days, Monday = 0
const MAX_SCHEDULE = 8;
const APP_PARTITION = 0x1c0000;  // largest firmware.bin (partitions.csv)
const GROUP_KEY = "ledframe.deviceGroup";

// --- Small form helpers -------------------------------------------------------------

function field(label, input, hint) {
  return h("label", { class: "field" }, h("span", {}, label), input,
           hint ? h("small", {}, hint) : null);
}

function number(value, min, max) {
  return h("input", { type: "number", min, max, step: 1, value, inputmode: "numeric" });
}

function toggle(checked) {
  return h("input", { type: "checkbox", class: "switch", role: "switch", checked });
}

function choice(options, value) {
  const select = h("select", {}, ...options.map(([v, text]) => h("option", { value: v }, text)));
  select.value = String(value);
  return select;
}

function card(title, ...children) {
  return h("section", { class: "card" }, h("h3", {}, title), ...children);
}

function status() {
  return h("p", { class: "form-status", role: "status" });
}

// Runs a request, shows "Saving…", then the result or the device's error message.
async function submit(button, out, action, done = "Saved") {
  button.disabled = true;
  out.textContent = "Saving…";
  out.dataset.kind = "";
  try {
    await action();
    out.textContent = done;
    out.dataset.kind = "ok";
    return true;
  } catch (error) {
    out.textContent = error.message;
    out.dataset.kind = "error";
    return false;
  } finally {
    button.disabled = false;
  }
}

// --- Settings ------------------------------------------------------------------------

// api: { getJson, postJson, device, status, group }: status is the status bar, group
// the settings group to show first. Returns { showGroup(id), destroy() }.
export async function mountSettings(container, api) {
  const { getJson, postJson, device, status: statusBar } = api;
  container.replaceChildren(h("p", { class: "hint" }, "Loading…"));
  let config = await getJson("/api/config");
  let info = statusBar.current() || await getJson("/api/info");

  // --- LED -------------------------------------------------------------------------
  const layout = config.layout;
  const width = number(layout.width, 1, info.maxLeds);
  const height = number(layout.height, 1, info.maxLeds);
  const serpentine = toggle(layout.serpentine);
  const flipX = toggle(layout.flipX);
  const flipY = toggle(layout.flipY);
  const origin = choice(ORIGINS.map((o) => [o, o.replace("-", " ")]), layout.origin);
  const rotation = choice(ROTATIONS.map((r) => [r, `${r}°`]), layout.rotation);
  const volts = number(config.power.volts, 1, 24);
  const milliamps = number(config.power.milliamps, 0, 20000);
  const dither = toggle(config.dither !== false);
  const gamma = toggle(config.gamma === true);
  // The data pin applies after a restart; info.ledPin is the one in use now.
  const ledPin = choice((info.ledPins || [config.ledPin]).map((p) => [p, `GPIO ${p}`]), config.ledPin);
  const pinHint = () => (config.ledPin !== info.ledPin
    ? `GPIO ${info.ledPin} in use until a restart`
    : "Strip data line; applies after a restart");
  const ledPinField = field("Data pin", ledPin, pinHint());
  const fpsValue = h("output", {}, String(config.fps));
  const fps = h("input", { type: "range", min: 10, max: 120, step: 1, value: config.fps,
                           oninput: () => (fpsValue.value = fps.value) });
  const ledSave = h("button", { type: "submit", class: "primary" }, "Save LED settings");
  const ledStatus = status();
  const ledForm = h(
    "form",
    {
      onsubmit: async (event) => {
        event.preventDefault();
        const w = Number(width.value);
        const hgt = Number(height.value);
        if (!(w >= 1 && hgt >= 1 && w * hgt <= info.maxLeds)) {
          ledStatus.textContent = `Width × height must be 1–${info.maxLeds} LEDs.`;
          ledStatus.dataset.kind = "error";
          return;
        }
        const pin = Number(ledPin.value);
        const pinMoved = pin !== info.ledPin;
        if (await submit(ledSave, ledStatus, () =>
          postJson("/api/config", {
            layout: {
              width: w, height: hgt, serpentine: serpentine.checked, origin: origin.value,
              rotation: Number(rotation.value), flipX: flipX.checked, flipY: flipY.checked,
            },
            ledPin: pin,
            power: { volts: Number(volts.value), milliamps: Number(milliamps.value) },
            fps: Number(fps.value),
            dither: dither.checked,
            gamma: gamma.checked,
          }),
          pinMoved ? `Saved. The strip moves to GPIO ${pin} after a restart (System tab).` : "Saved",
        )) {
          config.ledPin = pin;
          ledPinField.querySelector("small").textContent = pinHint();
        }
      },
    },
    h("div", { class: "form-grid" },
      field("Width", width), field("Height", height),
      field("Start corner", origin, "Where the strip starts"), field("Rotation", rotation),
      field("Serpentine", serpentine, "Every other row runs back"),
      field("Flip horizontally", flipX), field("Flip vertically", flipY)),
    h("div", { class: "form-grid" },
      ledPinField,
      field("Supply voltage (V)", volts),
      field("Current limit (mA)", milliamps, "0 = no limit"),
      field("Frames per second", h("div", { class: "row" }, fps, fpsValue)),
      field("Dithering", dither, "Smoother dim colors; off if still colors look uneven"),
      field("Color correction", gamma, "Chosen colors look as on screen (gamma 2.2)")),
    h("div", { class: "actions" }, ledSave, ledStatus),
  );

  // Color order applies at once, so the test pattern shows the effect of each choice.
  const colorOrder = choice(COLOR_ORDERS.map((name, i) => [i, name]), config.colorOrder);
  const orderStatus = status();
  colorOrder.addEventListener("change", () =>
    submit(colorOrder, orderStatus, () => postJson("/api/config", { colorOrder: Number(colorOrder.value) }),
           "Applied"));
  let testOn = info.testPattern;
  const testButton = h("button", { type: "button", "aria-pressed": String(testOn) });
  const showTest = () => (testButton.textContent = testOn ? "Back to the effect" : "Show test pattern");
  showTest();
  testButton.addEventListener("click", async () => {
    const next = !testOn;
    if (await submit(testButton, orderStatus, () => postJson("/api/test-pattern", { on: next }),
                     next ? "Test pattern on" : "Test pattern off")) {
      testOn = next;
      testButton.setAttribute("aria-pressed", String(testOn));
      showTest();
    }
  });
  const ledCard = card(
    "LEDs",
    ledForm,
    h("div", { class: "form-grid order" },
      field("Color order", colorOrder,
            "With the test pattern: the first LED is white, then red, green and blue thirds."),
      h("div", { class: "field" }, h("span", {}, "Test pattern"), testButton)),
    orderStatus,
  );

  // --- Network ---------------------------------------------------------------------
  const wifiState = h("p", { class: "wifi-state" });
  const ssid = h("input", { type: "text", maxlength: 32, value: config.wifi.ssid, autocomplete: "off" });
  const password = h("input", {
    type: "password", maxlength: 64, autocomplete: "new-password",
    placeholder: config.wifi.hasPassword ? "unchanged" : "",
  });
  const networks = h("div", { class: "networks" });
  const scanButton = h("button", { type: "button" }, "Scan");
  const wifiSave = h("button", { type: "submit", class: "primary" }, "Save network");
  const wifiStatus = status();

  const showWifi = () => {
    const w = info.wifi;
    const text = {
      connected: `Connected to “${w.ssid}”, ${w.ip}, signal ${w.rssi} dBm`,
      connecting: `Connecting to “${w.ssid}”…`,
      ap: `Access point “${w.apName}” at ${w.ip}, ${w.apClients} connected`,
      off: "Wi-Fi off",
    }[w.mode];
    wifiState.textContent = text || w.mode;
  };

  scanButton.addEventListener("click", async () => {
    scanButton.disabled = true;
    networks.replaceChildren(h("p", { class: "hint" }, "Scanning…"));
    try {
      await postJson("/api/wifi/scan", {});
      let result = await getJson("/api/wifi/scan");
      for (let i = 0; result.scanning && i < 20; ++i) {
        await new Promise((resolve) => setTimeout(resolve, 750));
        result = await getJson("/api/wifi/scan");
      }
      networks.replaceChildren(
        ...(result.networks.length
          ? result.networks.map((n) =>
              h("button", { type: "button", class: "network",
                            onclick: () => { ssid.value = n.ssid; password.focus(); } },
                n.ssid, h("small", {}, `${n.rssi} dBm${n.secure ? " · secured" : " · open"}`)))
          : [h("p", { class: "hint" }, "No networks found.")]),
      );
    } catch (error) {
      networks.replaceChildren(h("p", { class: "hint" }, error.message));
    } finally {
      scanButton.disabled = false;
    }
  });

  const wifiForm = h(
    "form",
    {
      onsubmit: async (event) => {
        event.preventDefault();
        const wifi = { ssid: ssid.value };
        // An empty field keeps the stored password for the same network; for a new
        // network it means an open network.
        if (password.value !== "" || ssid.value !== config.wifi.ssid) {
          wifi.password = password.value;
        }
        if (wifi.password && wifi.password.length < 8) {
          wifiStatus.textContent = "The password needs 8–64 characters, or leave it empty.";
          wifiStatus.dataset.kind = "error";
          return;
        }
        if (await submit(wifiSave, wifiStatus, () => postJson("/api/config", { wifi }),
                         "Saved. The device is joining the network; this page may lose the connection.")) {
          password.value = "";
          config = await getJson("/api/config");
        }
      },
    },
    h("div", { class: "form-grid" },
      field("Network name (SSID)", ssid, "Empty: access point only"),
      field("Password", password)),
    h("div", { class: "actions" }, wifiSave, scanButton, wifiStatus),
    networks,
  );
  const wifiCard = card("Network", wifiState, wifiForm);

  // --- System ----------------------------------------------------------------------
  const hostname = h("input", { type: "text", maxlength: 32, value: config.hostname, autocomplete: "off" });
  const otaPassword = h("input", {
    type: "password", maxlength: 64, autocomplete: "new-password",
    placeholder: config.ota.hasPassword ? "unchanged" : "none",
  });
  const otaRemove = toggle(false);
  const known = TIMEZONES.some(([rule]) => rule === config.timezone);
  const zone = choice([...TIMEZONES, [CUSTOM, "Other (POSIX TZ rule)"]], known ? config.timezone : CUSTOM);
  const zoneRule = h("input", { type: "text", maxlength: 63, value: config.timezone, autocomplete: "off",
                                spellcheck: false, placeholder: "e.g. CET-1CEST,M3.5.0,M10.5.0/3" });
  const zoneRuleField = field("Time zone rule", zoneRule, "POSIX TZ, as in the TZ variable");
  const showZoneRule = () => (zoneRuleField.hidden = zone.value !== CUSTOM);
  zone.addEventListener("change", () => {
    if (zone.value !== CUSTOM) {
      zoneRule.value = zone.value;
    }
    showZoneRule();
  });
  showZoneRule();
  const systemSave = h("button", { type: "submit", class: "primary" }, "Save system settings");
  const systemStatus = status();
  const restart = h("button", { type: "button", class: "danger" }, "Restart device");
  restart.addEventListener("click", async () => {
    if (window.confirm("Restart the device now?")) {
      await submit(restart, systemStatus, () => postJson("/api/restart", {}), "Restarting…");
    }
  });
  const systemForm = h(
    "form",
    {
      onsubmit: async (event) => {
        event.preventDefault();
        if (!HOSTNAME.test(hostname.value)) {
          systemStatus.textContent = "Hostname: 1–32 lowercase letters, digits and '-', not at either end.";
          systemStatus.dataset.kind = "error";
          return;
        }
        const timezone = zone.value === CUSTOM ? zoneRule.value.trim() : zone.value;
        if (!TIMEZONE.test(timezone)) {
          systemStatus.textContent = "Time zone: a POSIX TZ rule, e.g. CET-1CEST,M3.5.0,M10.5.0/3.";
          systemStatus.dataset.kind = "error";
          return;
        }
        const body = { hostname: hostname.value, timezone };
        if (otaRemove.checked) {
          body.ota = { password: "" };
        } else if (otaPassword.value !== "") {
          body.ota = { password: otaPassword.value };
        }
        const renamed = hostname.value !== config.hostname;
        if (await submit(systemSave, systemStatus, () => postJson("/api/config", body),
                         renamed ? "Saved. The new hostname applies after a restart." : "Saved")) {
          otaPassword.value = "";
          otaRemove.checked = false;
          config = await getJson("/api/config");
          otaPassword.placeholder = config.ota.hasPassword ? "unchanged" : "none";
          updateAuth();
        }
      },
    },
    h("div", { class: "form-grid" },
      field("Hostname", hostname, `Address: http://${config.hostname}.local`),
      field("OTA password", otaPassword, "For firmware updates; empty keeps it"),
      field("Remove OTA password", otaRemove),
      field("Time zone", zone, "Local time from the internet (NTP) once connected"),
      zoneRuleField),
    h("div", { class: "actions" }, systemSave, restart, systemStatus),
  );
  const systemCard = card("System", systemForm);

  // --- Schedule ----------------------------------------------------------------------
  // One row per entry: on/off switch for the entry, time, action and weekdays. The
  // whole list is saved at once ({"schedule": [...]} replaces it on the device).
  const rows = h("div", { class: "schedule" });
  const scheduleStatus = status();
  const addButton = h("button", { type: "button" }, "Add time");
  const showAdd = () => (addButton.disabled = rows.children.length >= MAX_SCHEDULE);
  const addRow = (entry) => {
    const enabled = toggle(entry.enabled !== false);
    enabled.title = "Entry active";
    const time = h("input", { type: "time", value: entry.time, required: true });
    const action = choice([["on", "Switch on"], ["off", "Switch off"]], entry.action);
    const days = DAYS.map((name, day) => h("input", { type: "checkbox", checked: entry.days.includes(day),
                                                       title: name }));
    const remove = h("button", { type: "button", class: "danger", title: "Remove" }, "×");
    const row = h("div", { class: "schedule-row" }, enabled, time, action,
                  h("span", { class: "days" }, ...days.map((box, i) => h("label", {}, box, DAYS[i]))), remove);
    row.read = () => ({
      enabled: enabled.checked,
      time: time.value,
      action: action.value,
      days: days.flatMap((box, day) => (box.checked ? [day] : [])),
    });
    remove.addEventListener("click", () => {
      row.remove();
      showAdd();
    });
    rows.append(row);
    showAdd();
  };
  (config.schedule || []).forEach(addRow);
  addButton.addEventListener("click", () => addRow({ time: "07:00", action: "on", days: [0, 1, 2, 3, 4] }));
  const scheduleSave = h("button", { type: "button", class: "primary" }, "Save schedule");
  scheduleSave.addEventListener("click", async () => {
    const schedule = [...rows.children].map((row) => row.read());
    if (schedule.some((entry) => !/^\d\d:\d\d$/.test(entry.time) || entry.days.length === 0)) {
      scheduleStatus.textContent = "Every time needs a time and at least one day.";
      scheduleStatus.dataset.kind = "error";
      return;
    }
    await submit(scheduleSave, scheduleStatus, () => postJson("/api/config", { schedule }));
  });
  const scheduleCard = card(
    "Schedule",
    h("p", { class: "hint" }, "Switches the LEDs on or off at these local times. Needs the time from the internet."),
    rows,
    h("div", { class: "actions" }, addButton, scheduleSave, scheduleStatus),
  );

  // --- Firmware update -------------------------------------------------------------
  const file = h("input", { type: "file", accept: ".bin,application/octet-stream" });
  const updateAuthField = h("input", { type: "password", autocomplete: "current-password" });
  const authRow = field("OTA password", updateAuthField);
  const updateAuth = () => (authRow.hidden = !config.ota.hasPassword);
  updateAuth();
  const progress = h("progress", { max: 100, value: 0, hidden: true });
  const uploadButton = h("button", { type: "button", class: "primary" }, "Upload firmware");
  const updateStatus = status();
  uploadButton.addEventListener("click", () => {
    const firmware = file.files[0];
    if (!firmware) {
      updateStatus.textContent = "Choose firmware.bin first.";
      updateStatus.dataset.kind = "error";
      return;
    }
    if (firmware.size > APP_PARTITION) {
      updateStatus.textContent = "The file is larger than the firmware partition.";
      updateStatus.dataset.kind = "error";
      return;
    }
    const form = new FormData();
    form.append("firmware", firmware, firmware.name);
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/ota");
    // Without it a wrong password opens the browser's login dialog (Ota.cpp).
    xhr.setRequestHeader("X-Requested-With", "XMLHttpRequest");
    if (config.ota.hasPassword) {
      xhr.setRequestHeader("Authorization", "Basic " + btoa(`ota:${updateAuthField.value}`));
    }
    xhr.upload.onprogress = (event) => {
      if (event.lengthComputable) {
        progress.value = Math.round((event.loaded * 100) / event.total);
        updateStatus.textContent = `Uploading… ${progress.value}%`;
      }
    };
    xhr.onload = () => {
      let message = `HTTP ${xhr.status}`;
      try {
        const body = JSON.parse(xhr.responseText);
        message = body.error || message;
      } catch {
        // Keep the status code.
      }
      if (xhr.status === 200) {
        progress.value = 100;
        updateStatus.textContent = "Updated. The device restarts; the page reloads when it is back.";
        updateStatus.dataset.kind = "ok";
        // The new firmware may bring a new web UI.
        device.on("open", () => window.location.reload());
      } else {
        updateStatus.textContent = xhr.status === 401 ? "Wrong OTA password." : message;
        updateStatus.dataset.kind = "error";
        uploadButton.disabled = false;
      }
    };
    xhr.onerror = () => {
      updateStatus.textContent = "Upload failed: connection lost.";
      updateStatus.dataset.kind = "error";
      uploadButton.disabled = false;
    };
    uploadButton.disabled = true;
    progress.hidden = false;
    progress.value = 0;
    updateStatus.dataset.kind = "";
    xhr.send(form);
  });
  const updateCard = card(
    "Firmware update",
    h("p", { class: "hint" }, "Upload .pio/build/esp32dev/firmware.bin. Settings and the picture are kept."),
    h("div", { class: "form-grid" }, field("Firmware file", file), authRow),
    h("div", { class: "actions" }, uploadButton, updateStatus),
    progress,
  );

  // The status bar fetches /api/info; the Wi-Fi line and the test pattern follow it.
  const stopInfo = statusBar.onInfo((next) => {
    info = next;
    showWifi();
    if (info.testPattern !== testOn) {
      testOn = info.testPattern;  // Another page or a state change ended it.
      testButton.setAttribute("aria-pressed", String(testOn));
      showTest();
    }
  });
  showWifi();

  // --- Groups behind tabs ------------------------------------------------------------
  const groups = [
    ["leds", "LEDs", ledCard],
    ["network", "Network", wifiCard],
    ["system", "System", systemCard],
    ["schedule", "Schedule", scheduleCard],
    ["update", "Firmware", updateCard],
  ];
  const tabs = groups.map(([id, label]) =>
    h("a", { href: `#device/${id}`, role: "tab", "aria-selected": "false" }, label));
  const showGroup = (wanted) => {
    let stored = null;
    try {
      stored = localStorage.getItem(GROUP_KEY);
    } catch {
      // Blocked storage: start with the first group.
    }
    const id = [wanted, stored].find((g) => groups.some(([gid]) => gid === g)) || groups[0][0];
    groups.forEach(([gid, , pane], i) => {
      pane.hidden = gid !== id;
      tabs[i].setAttribute("aria-selected", String(gid === id));
    });
    try {
      localStorage.setItem(GROUP_KEY, id);
    } catch {
      // The choice is kept for this visit only.
    }
  };
  showGroup(api.group);
  statusBar.refresh();  // fresh Wi-Fi state and test pattern for this page

  container.replaceChildren(h("nav", { class: "subtabs", role: "tablist", "aria-label": "Device settings" }, ...tabs),
                            ...groups.map(([, , pane]) => pane));
  return {
    showGroup,
    destroy() {
      stopInfo();
    },
  };
}

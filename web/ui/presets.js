// Presets: a tap applies a preset, the current state
// is saved under a name, and each preset can be renamed or deleted.
//
// The list comes from GET /api/presets. Preset changes are not broadcast over the
// WebSocket, so the list is loaded again after own changes, after a reconnect and
// when the page comes back to the foreground.

import { briPercent, h } from "./controls.js";

// api: { getJson, requestJson, device, effects }. Returns { destroy() }.
export function mountPresets(container, api) {
  const { getJson, requestJson, device } = api;
  let presets = [];

  const list = h("div", { class: "presets" });
  const name = h("input", {
    type: "text", maxlength: 32, placeholder: "Name for the current state", "aria-label": "Preset name",
  });
  const save = h("button", { type: "submit", class: "primary" }, "Save");
  const status = h("p", { class: "form-status", role: "status" });
  const form = h("form", { class: "preset-save", onsubmit: (event) => saveCurrent(event) }, name, save);
  container.replaceChildren(list, form, status);

  const say = (text, kind = "") => {
    status.textContent = text;
    status.dataset.kind = kind;
  };

  const effectName = (id) => api.effects().find((e) => e.id === id)?.name || id;

  async function run(action, done) {
    try {
      await action();
      say(done, "ok");
    } catch (error) {
      say(error.message, "error");
    }
    await load();
  }

  function render() {
    if (presets.length === 0) {
      list.replaceChildren(h("p", { class: "hint" }, "No presets yet. Save the current state below."));
      return;
    }
    list.replaceChildren(
      ...presets.map((p) =>
        h(
          "div",
          { class: "preset" },
          h(
            "button",
            {
              type: "button", class: "apply", title: `Apply “${p.name}”`,
              onclick: () => run(() => requestJson("POST", `/api/presets/${p.id}/apply`), `Applied “${p.name}”`),
            },
            p.name,
            h("small", {}, `${effectName(p.effect)} · ${briPercent(p.bri)}`),
          ),
          h("button", {
            type: "button", class: "icon", title: "Rename", "aria-label": `Rename “${p.name}”`,
            onclick: () => {
              const next = window.prompt("New name", p.name);
              if (next !== null && next.trim() !== "" && next !== p.name) {
                run(() => requestJson("POST", `/api/presets/${p.id}`, { name: next.trim() }), "Renamed");
              }
            },
          }, "✎"),
          h("button", {
            type: "button", class: "icon", title: "Delete", "aria-label": `Delete “${p.name}”`,
            onclick: () => {
              if (window.confirm(`Delete the preset “${p.name}”?`)) {
                run(() => requestJson("DELETE", `/api/presets/${p.id}`), "Deleted");
              }
            },
          }, "✕"),
        ),
      ),
    );
  }

  async function load() {
    try {
      presets = await getJson("/api/presets");
      render();
    } catch {
      // Offline: the list stays; it loads again on reconnect.
    }
  }

  async function saveCurrent(event) {
    event.preventDefault();
    const text = name.value.trim();
    if (!text) {
      say("Enter a name.", "error");
      return;
    }
    // Saving under an existing name replaces that preset (POST /api/presets).
    if (presets.some((p) => p.name === text) &&
        !window.confirm(`Replace the preset “${text}” with the current state?`)) {
      return;
    }
    save.disabled = true;
    await run(() => requestJson("POST", "/api/presets", { name: text }), `Saved “${text}”`);
    save.disabled = false;
    name.value = "";
  }

  const onVisible = () => !document.hidden && load();
  document.addEventListener("visibilitychange", onVisible);
  const offOpen = device.on("open", load);
  load();

  return {
    destroy() {
      document.removeEventListener("visibilitychange", onVisible);
      offOpen();
    },
  };
}

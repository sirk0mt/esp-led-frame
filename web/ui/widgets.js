// Loads extra UI components for effects with a "widget" field.
//
// A widget is the module web/ui/widgets/<name>.js, loaded on first use. It exports
//   mount(container, ctx) -> { update(state)?, destroy()? }
// where ctx is
//   effect      the effect descriptor from GET /api/effects
//   state       the current state (GET /api/state format)
//   send(patch, now)  state patch, as from the parameter panel
//   sendBinary(data)  binary WebSocket message; false without a connection
//   on(event, fn)     "binary": fn(Uint8Array) per binary message; "open": fn()
//                     after every (re)connect. Returns a function that removes it.
//   getJson(path), postJson(path, body)  REST helpers
// update(state) runs on every new state, destroy() when another effect is chosen.
// app.js loads this module only when an effect has a widget.

import { h } from "./controls.js";

const modules = new Map();

function load(name) {
  if (!/^[a-z0-9_-]+$/.test(name)) {
    return Promise.reject(new Error(`invalid widget name '${name}'`));
  }
  if (!modules.has(name)) {
    // Failed loads are not cached: the next try loads again.
    const promise = import(`./widgets/${name}.js`).catch((error) => {
      modules.delete(name);
      throw error;
    });
    modules.set(name, promise);
  }
  return modules.get(name);
}

// Mounts the widget into container and returns { update, destroy }. A missing or
// broken widget shows a message instead; the parameter panel keeps working.
export async function mountWidget(name, container, ctx) {
  try {
    const module = await load(name);
    const instance = module.mount(container, ctx) || {};
    return {
      update: (state) => instance.update?.(state),
      destroy: () => {
        instance.destroy?.();
        container.replaceChildren();
      },
    };
  } catch (error) {
    console.warn(`widget ${name}:`, error);
    container.replaceChildren(
      h("p", { class: "widget-error" }, `The "${name}" editor is not available in this firmware.`),
    );
    return { update() {}, destroy: () => container.replaceChildren() };
  }
}

// UI building blocks: DOM helpers, effect tiles and the parameter panel.
//
// One module on purpose: the device accepts only a few connections at once,
// so the page loads as few files in parallel as possible.

// --- Helpers -------------------------------------------------------------------

// h("div", { class: "x", onclick: fn }, child, "text") -> element
export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) {
      continue;
    }
    if (key.startsWith("on")) {
      el.addEventListener(key.slice(2), value);
    } else if (key in el) {
      el[key] = value;
    } else {
      el.setAttribute(key, value === true ? "" : value);
    }
  }
  el.append(...children.filter((child) => child !== null && child !== undefined));
  return el;
}

function toHex(value) {
  return "#" + (value & 0xffffff).toString(16).padStart(6, "0");
}

function fromHex(text) {
  return parseInt(text.slice(1), 16);
}

function gradient(colors) {
  return `linear-gradient(90deg, ${colors.join(", ")})`;
}

// A control the user just changed ignores incoming state for a moment. Otherwise an
// older broadcast, sent before the device got the change, moves it back.
const HOLD_MS = 600;
const holds = new Map();

export function hold(key) {
  holds.set(key, Date.now() + HOLD_MS);
}

export function held(key) {
  return (holds.get(key) || 0) > Date.now();
}

// --- Brightness curve --------------------------------------------------------------
// The slider has 1000 positions on a square curve, because the eye is most sensitive
// at the dim end: bri = round(255 * (p / 1000)^2), at least 1 above 0. Every value
// 0..255 is reachable and briToPosition() gives back the exact position. Percent is
// the slider position, as the eye sees it.

export const BRI_STEPS = 1000;

export function positionToBri(p) {
  return p <= 0 ? 0 : Math.max(1, Math.round(255 * (p / BRI_STEPS) ** 2));
}

export function briToPosition(bri) {
  return bri <= 0 ? 0 : Math.round(BRI_STEPS * Math.sqrt(bri / 255));
}

export function briPercent(bri) {
  return `${Math.round((briToPosition(bri) * 100) / BRI_STEPS)}%`;
}

// --- LED grid --------------------------------------------------------------------
// The matrix on a <canvas>: one square per LED, row by row. rgb is a Uint8Array with
// 3 bytes per pixel (r, g, b). Used by the live preview and the paint widget.

const UNLIT = "#26282d";  // a dark LED stays visible as a cell

// Sizes the canvas to fill its parent's width, at most maxCell CSS pixels per LED,
// sharp on high-density screens. Returns the cell size in CSS pixels.
export function sizeGrid(canvas, width, height, maxCell = 32) {
  const available = canvas.parentElement?.clientWidth || 320;
  const cell = Math.max(4, Math.min(maxCell, Math.floor(available / width)));
  const ratio = window.devicePixelRatio || 1;
  canvas.style.width = `${cell * width}px`;
  canvas.style.height = `${cell * height}px`;
  canvas.width = Math.round(cell * width * ratio);
  canvas.height = Math.round(cell * height * ratio);
  return cell;
}

export function drawCell(canvas, x, y, r, g, b, width) {
  const ctx = canvas.getContext("2d");
  const cell = canvas.width / width;
  const gap = Math.max(1, Math.round(cell * 0.12));
  ctx.clearRect(x * cell, y * cell, cell, cell);
  ctx.fillStyle = r | g | b ? `rgb(${r},${g},${b})` : UNLIT;
  ctx.fillRect(x * cell + gap / 2, y * cell + gap / 2, cell - gap, cell - gap);
}

export function drawGrid(canvas, rgb, width, height) {
  canvas.getContext("2d").clearRect(0, 0, canvas.width, canvas.height);
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const i = (y * width + x) * 3;
      drawCell(canvas, x, y, rgb[i], rgb[i + 1], rgb[i + 2], width);
    }
  }
}

// --- Effect tiles ----------------------------------------------------------------
// Built from GET /api/effects: a new effect in the firmware shows up without
// changes in web/.

function describe(effect) {
  if (effect.widget) {
    return "Drawing";
  }
  const count = effect.params.length;
  return count === 0 ? "No settings" : `${count} setting${count === 1 ? "" : "s"}`;
}

// Returns { setActive(id) }. onSelect(id) runs when a tile is clicked.
export function renderEffects(container, effects, onSelect) {
  const tiles = new Map();
  container.replaceChildren(
    ...effects.map((effect) => {
      const tile = h(
        "button",
        {
          class: "tile",
          type: "button",
          role: "radio",
          "aria-checked": "false",
          onclick: () => onSelect(effect.id),
        },
        effect.name,
        h("small", {}, describe(effect)),
      );
      tiles.set(effect.id, tile);
      return tile;
    }),
  );
  return {
    setActive(id) {
      for (const [tileId, tile] of tiles) {
        tile.setAttribute("aria-checked", String(tileId === id));
      }
    },
  };
}

// --- Parameter panel --------------------------------------------------------------
// Generated from the effect schema (GET /api/effects):
//   slider -> range input, toggle -> switch, color -> color picker,
//   select -> list, palette -> palette list with preview strips.
// Every control is { row, set(value) }; change(value, now) sends a new value.

function slider(param, change) {
  const output = h("output", {}, String(param.default));
  // min and max before value: a range input clamps value to its current range.
  const input = h("input", {
    type: "range",
    min: param.min,
    max: param.max,
    step: 1,
    value: param.default,
    "aria-label": param.label,
    oninput: () => {
      output.value = input.value;
      change(Number(input.value), false);
    },
    onchange: () => change(Number(input.value), true),
  });
  return {
    row: h("div", { class: "row" }, input, output),
    set(value) {
      input.value = value;
      output.value = String(value);
    },
  };
}

function toggle(param, change) {
  const input = h("input", {
    type: "checkbox",
    class: "switch",
    role: "switch",
    "aria-label": param.label,
    onchange: () => change(input.checked ? 1 : 0, true),
  });
  return {
    row: h("div", { class: "row" }, input),
    set(value) {
      input.checked = value !== 0;
    },
  };
}

function color(param, change) {
  const input = h("input", {
    type: "color",
    value: toHex(param.default),
    "aria-label": param.label,
    oninput: () => change(fromHex(input.value), false),
    onchange: () => change(fromHex(input.value), true),
  });
  return {
    row: h("div", { class: "row" }, input),
    set(value) {
      input.value = toHex(value);
    },
  };
}

function select(param, change) {
  const input = h(
    "select",
    { "aria-label": param.label, onchange: () => change(Number(input.value), true) },
    ...param.options.map((name, index) => h("option", { value: index }, name)),
  );
  return {
    row: h("div", { class: "row" }, input),
    set(value) {
      input.value = String(value);
    },
  };
}

function palette(label, palettes, change) {
  const buttons = palettes.map((p) =>
    h(
      "button",
      {
        class: "palette",
        type: "button",
        role: "radio",
        "aria-checked": "false",
        onclick: () => {
          set(p.index);  // Buttons keep no value of their own: mark the choice at once.
          change(p.index, true);
        },
      },
      p.name,
      h("i", { style: `background: ${gradient(p.colors)}` }),
    ),
  );
  const set = (value) => {
    palettes.forEach((p, i) => buttons[i].setAttribute("aria-checked", String(p.index === value)));
  };
  return {
    row: h("div", { class: "palettes", role: "radiogroup", "aria-label": label }, ...buttons),
    set,
  };
}

const builders = { slider, toggle, color, select };

// Builds the panel for one effect. send(patch, now) delivers a state patch.
// Returns { update(state) }, which shows new values unless the user is changing them.
export function renderParams(container, effect, palettes, send) {
  const controls = [];
  const add = (key, label, control) => {
    controls.push({ key, control });
    container.append(h("div", { class: "param" }, h("span", {}, label), control.row));
  };

  container.replaceChildren();
  for (const param of effect.params) {
    const key = `params.${param.id}`;
    const change = (value, now) => {
      hold(key);
      send({ params: { [param.id]: value } }, now);
    };
    const control =
      param.kind === "palette"
        ? palette(param.label, palettes, change)
        : (builders[param.kind] || slider)(param, change);
    add(key, param.label, control);
  }

  // An effect that uses the palette without its own palette parameter gets the
  // global palette.
  if (effect.usesPalette && !effect.params.some((p) => p.kind === "palette")) {
    add(
      "palette",
      "Palette",
      palette("Palette", palettes, (value, now) => {
        hold("palette");
        send({ palette: value }, now);
      }),
    );
  }

  return {
    update(state) {
      for (const { key, control } of controls) {
        if (held(key)) {
          continue;
        }
        const value = key === "palette" ? state.palette : state.params?.[key.slice(7)];
        if (value !== undefined) {
          control.set(value);
        }
      }
    },
  };
}

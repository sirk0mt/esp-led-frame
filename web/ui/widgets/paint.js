// Paint widget: draws the picture of the "paint" effect (docs/API.md).
//
// The picture comes from GET /api/paint. Strokes change the local copy at once and
// go to the device as binary paint messages, grouped every FLUSH_MS and at most
// MAX_PIXELS per message. Changes from other open pages arrive as the same paint
// messages, forwarded by the device, and are drawn here too.

import { drawCell, drawGrid, h, sizeGrid } from "../controls.js";

const FLUSH_MS = 50;
const RETRY_MS = 500;
const MAX_PIXELS = 292;  // largest paint message the device accepts
const HEADER = 8;        // "LP", version, 0, width u16, height u16

function rgbOf(hex) {
  const value = parseInt(hex.slice(1), 16);
  return [(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff];
}

// Paint message 0x01 0x00 with x u16, y u16, r, g, b per pixel (little-endian).
function pixelMessage(entries, width) {
  const message = new Uint8Array(2 + entries.length * 7);
  const view = new DataView(message.buffer);
  message[0] = 0x01;
  message[1] = 0x00;
  entries.forEach(([index, [r, g, b]], i) => {
    const offset = 2 + i * 7;
    view.setUint16(offset, index % width, true);
    view.setUint16(offset + 2, Math.floor(index / width), true);
    message[offset + 4] = r;
    message[offset + 5] = g;
    message[offset + 6] = b;
  });
  return message;
}

export function mount(container, ctx) {
  let width = 0;
  let height = 0;
  let pixels = new Uint8Array(0);  // r, g, b row by row
  const pending = new Map();  // pixel index -> [r, g, b], not sent yet
  let flushTimer = 0;
  let drawing = false;
  let last = null;  // last cell of the stroke
  let tool = "brush";

  const color = h("input", { type: "color", value: "#ff3000", "aria-label": "Color" });
  const toolButton = (name, label) =>
    h("button", { type: "button", class: "tool", "aria-pressed": String(name === tool),
                  onclick: () => setTool(name) }, label);
  const brush = toolButton("brush", "Brush");
  const eraser = toolButton("eraser", "Eraser");
  const fill = h("button", { type: "button", class: "tool", onclick: () => fillAll() }, "Fill");
  const clear = h("button", { type: "button", class: "tool", onclick: () => clearAll() }, "Clear");
  const canvas = h("canvas", { class: "grid paint", "aria-label": "Picture, draw with mouse or touch" });
  const note = h("p", { class: "hint" }, "Loading the picture…");
  container.replaceChildren(
    h("div", { class: "paint-tools" }, color, brush, eraser, fill, clear),
    h("div", { class: "grid-box" }, canvas),
    note,
  );

  function setTool(name) {
    tool = name;
    brush.setAttribute("aria-pressed", String(name === "brush"));
    eraser.setAttribute("aria-pressed", String(name === "eraser"));
  }

  function redraw() {
    sizeGrid(canvas, width, height);
    drawGrid(canvas, pixels, width, height);
  }

  async function load() {
    try {
      const response = await fetch("/api/paint", { cache: "no-store" });
      const data = new Uint8Array(await response.arrayBuffer());
      if (data.length < HEADER || data[0] !== 0x4c || data[1] !== 0x50) {
        throw new Error("bad picture");
      }
      width = data[4] | (data[5] << 8);
      height = data[6] | (data[7] << 8);
      pixels = data.slice(HEADER, HEADER + width * height * 3);
      // Strokes not yet sent stay on top of the loaded picture.
      for (const [index, rgb] of pending) {
        pixels.set(rgb, index * 3);
      }
      redraw();
      note.textContent = "";
    } catch {
      note.textContent = "Cannot load the picture.";
    }
  }

  function setPixel(x, y, rgb) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return;
    }
    const index = y * width + x;
    pixels.set(rgb, index * 3);
    drawCell(canvas, x, y, rgb[0], rgb[1], rgb[2], width);
    pending.set(index, rgb);
    if (!flushTimer) {
      flushTimer = setTimeout(flush, FLUSH_MS);
    }
  }

  // Sends pending pixels; without a connection they wait and are sent later.
  function flush() {
    flushTimer = 0;
    if (pending.size === 0) {
      return;
    }
    const entries = [...pending];
    for (let start = 0; start < entries.length; start += MAX_PIXELS) {
      const part = entries.slice(start, start + MAX_PIXELS);
      if (!ctx.sendBinary(pixelMessage(part, width))) {
        note.textContent = "Not connected: the drawing is sent when the device is back.";
        flushTimer = setTimeout(flush, RETRY_MS);
        return;
      }
      for (const [index] of part) {
        pending.delete(index);
      }
    }
    note.textContent = "";
  }

  function command(bytes, rgb) {
    if (!ctx.sendBinary(new Uint8Array(bytes))) {
      note.textContent = "Not connected.";
      return;
    }
    pending.clear();
    for (let i = 0; i < width * height; ++i) {
      pixels.set(rgb, i * 3);
    }
    drawGrid(canvas, pixels, width, height);
  }

  function fillAll() {
    const rgb = rgbOf(color.value);
    command([0x01, 0x02, ...rgb], rgb);
  }

  function clearAll() {
    if (window.confirm("Clear the whole picture?")) {
      command([0x01, 0x01], [0, 0, 0]);
    }
  }

  // --- Drawing with mouse, pen and touch ---------------------------------------

  function cellAt(event) {
    const rect = canvas.getBoundingClientRect();
    return [
      Math.floor(((event.clientX - rect.left) / rect.width) * width),
      Math.floor(((event.clientY - rect.top) / rect.height) * height),
    ];
  }

  // Every cell on the line from the previous point, so fast strokes have no gaps.
  function strokeTo([x1, y1]) {
    const rgb = tool === "eraser" ? [0, 0, 0] : rgbOf(color.value);
    let [x0, y0] = last || [x1, y1];
    const dx = Math.abs(x1 - x0);
    const dy = -Math.abs(y1 - y0);
    const sx = x0 < x1 ? 1 : -1;
    const sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      setPixel(x0, y0, rgb);
      if (x0 === x1 && y0 === y1) {
        break;
      }
      const e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
    last = [x1, y1];
  }

  canvas.addEventListener("pointerdown", (event) => {
    if (width === 0) {
      return;
    }
    drawing = true;
    last = null;
    canvas.setPointerCapture(event.pointerId);
    strokeTo(cellAt(event));
    event.preventDefault();
  });
  canvas.addEventListener("pointermove", (event) => {
    if (drawing) {
      strokeTo(cellAt(event));
    }
  });
  const end = () => {
    drawing = false;
    last = null;
    flush();
  };
  canvas.addEventListener("pointerup", end);
  canvas.addEventListener("pointercancel", end);

  // --- Changes from other pages and reconnects ---------------------------------

  const offBinary = ctx.on("binary", (data) => {
    if (data[0] !== 0x01 || width === 0) {
      return;
    }
    if (data[1] === 0x00) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      for (let offset = 2; offset + 7 <= data.length; offset += 7) {
        const x = view.getUint16(offset, true);
        const y = view.getUint16(offset + 2, true);
        if (x < width && y < height && !pending.has(y * width + x)) {
          pixels.set(data.subarray(offset + 4, offset + 7), (y * width + x) * 3);
          drawCell(canvas, x, y, data[offset + 4], data[offset + 5], data[offset + 6], width);
        }
      }
    } else if (data[1] === 0x01 || data[1] === 0x02) {
      const rgb = data[1] === 0x01 ? [0, 0, 0] : [data[2], data[3], data[4]];
      for (let i = 0; i < width * height; ++i) {
        pixels.set(rgb, i * 3);
      }
      drawGrid(canvas, pixels, width, height);
    }
  });
  // Changes made while disconnected are missing here: load the picture again.
  const offOpen = ctx.on("open", load);
  const resize = new ResizeObserver(() => width && redraw());
  resize.observe(container);
  load();

  return {
    destroy() {
      clearTimeout(flushTimer);
      flush();  // A stroke in progress is not lost when switching the effect.
      offBinary();
      offOpen();
      resize.disconnect();
    },
  };
}

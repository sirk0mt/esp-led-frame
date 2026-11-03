// Live preview of the matrix. Loaded only when the user turns
// it on, so pages without it load less.
//
// Binary WebSocket: 0x02 0x01 starts the frames, 0x02 0x00 stops them. A frame is
// 0x02 0x10, width u16 LE, height u16 LE, then r, g, b per pixel row by row. The
// device sends a frame only when the picture changed, at most about 15 per second.

import { drawGrid, h, sizeGrid } from "./controls.js";

const START = new Uint8Array([0x02, 0x01]);
const STOP = new Uint8Array([0x02, 0x00]);
const HEADER = 6;

// Shows frames in container until stop() is called. device is the Device from api.js.
export function startPreview(container, device) {
  const canvas = h("canvas", { class: "grid", "aria-label": "Live preview of the LEDs" });
  const status = h("p", { class: "hint" }, "Waiting for the first frame…");
  container.replaceChildren(canvas, status);
  let size = null;  // [width, height] of the last frame
  let last = null;

  const show = (data) => {
    if (data.length < HEADER || data[0] !== 0x02 || data[1] !== 0x10) {
      return;
    }
    const width = data[2] | (data[3] << 8);
    const height = data[4] | (data[5] << 8);
    if (data.length !== HEADER + width * height * 3) {
      return;
    }
    if (!size || size[0] !== width || size[1] !== height) {
      size = [width, height];
      sizeGrid(canvas, width, height, 28);
      status.remove();
    }
    last = data.subarray(HEADER);
    drawGrid(canvas, last, width, height);
  };

  const subscribe = () => device.sendBinary(START);
  const offBinary = device.on("binary", show);
  const offOpen = device.on("open", subscribe);  // The device forgets it on reconnect.
  const resize = new ResizeObserver(() => {
    if (size && last) {
      sizeGrid(canvas, size[0], size[1], 28);
      drawGrid(canvas, last, size[0], size[1]);
    }
  });
  resize.observe(container);
  if (!subscribe()) {
    status.textContent = "Waiting for the connection…";
  }

  return {
    stop() {
      device.sendBinary(STOP);
      offBinary();
      offOpen();
      resize.disconnect();
      container.replaceChildren();
    },
  };
}

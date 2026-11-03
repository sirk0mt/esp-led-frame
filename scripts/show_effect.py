"""Shows effects in the console without hardware.

Renders effects with the native test test/test_render_dump (the real EffectEngine,
fake clock) and draws the frames in the terminal:

  color  24-bit color blocks, for people (Windows Terminal, VS Code, most Linux
         and macOS terminals); --play runs the animation in place
  ascii  plain text, for reading without colors (agents, logs): every LED is a hue
         letter and a brightness digit, and each row ends with its mean brightness

Examples (from the repository root):
  python scripts/show_effect.py fire
  python scripts/show_effect.py fire:speed=100,cooling=80 radial-rainbow:direction=inward
  python scripts/show_effect.py rainbow --play --loop
  python scripts/show_effect.py fire --mode ascii --frames 4
  python scripts/show_effect.py --dump frames.txt --mode ascii   # an existing dump

Without effect names every effect the UI shows is rendered with default parameters.
The first run builds the native test (about a minute); later runs take seconds.
"""

import argparse
import colorsys
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- Running the native test ---------------------------------------------------------


def find_pio():
    found = shutil.which("pio")
    if found:
        return found
    home = os.path.expanduser("~")
    for candidate in (os.path.join(home, ".platformio", "penv", "Scripts", "pio.exe"),
                      os.path.join(home, ".platformio", "penv", "bin", "pio")):
        if os.path.exists(candidate):
            return candidate
    sys.exit("show_effect: PlatformIO (pio) not found")


def render(args, dump_path):
    env = dict(os.environ)
    env["LEDFRAME_DUMP"] = dump_path
    env["LEDFRAME_DUMP_EFFECTS"] = ";".join(args.effects)
    env["LEDFRAME_DUMP_MS"] = str(args.ms)
    env["LEDFRAME_DUMP_FPS"] = str(args.fps)
    env["LEDFRAME_DUMP_SIZE"] = args.size
    env["LEDFRAME_DUMP_GAMMA"] = "1" if args.gamma else "0"
    # The native build needs gcc; on the author's computer it is in C:\mingw64\bin.
    # Its folder goes first even when gcc is found: the test program loads the
    # runtime DLLs (libstdc++) from PATH, and another copy earlier in PATH (Git for
    # Windows has one) stops it with 0xC0000139.
    gcc = shutil.which("gcc", path=env.get("PATH"))
    folder = os.path.dirname(gcc) if gcc else r"C:\mingw64\bin"
    if os.path.isdir(folder):
        env["PATH"] = folder + os.pathsep + env.get("PATH", "")
    result = subprocess.run(
        [find_pio(), "test", "-e", "native", "-f", "test_render_dump"],
        cwd=ROOT, env=env, capture_output=True, text=True, errors="replace")
    if result.returncode != 0 or not os.path.exists(dump_path):
        lines = (result.stdout + result.stderr).strip().splitlines()
        print("\n".join(lines[-30:]), file=sys.stderr)
        sys.exit("show_effect: rendering failed")


# --- Reading the dump ----------------------------------------------------------------


def read_dump(path):
    """Returns [{id, width, height, fps, params, frames: [(nowMs, rows)]}]."""
    effects = []
    with open(path, encoding="ascii") as file:
        lines = iter(file.read().splitlines())
    if next(lines, "") != "ledframe-dump 1":
        sys.exit(f"show_effect: {path} is not a ledframe dump")
    current = None
    for line in lines:
        words = line.split()
        if not words:
            continue
        if words[0] == "effect":
            current = {"id": words[1], "width": int(words[2]), "height": int(words[3]),
                       "fps": int(words[4]), "params": "", "frames": []}
            effects.append(current)
        elif words[0] == "params":
            current["params"] = " ".join(words[1:])
        elif words[0] == "frame":
            rows = []
            for _ in range(current["height"]):
                rows.append([tuple(int(w[i:i + 2], 16) for i in (0, 2, 4))
                             for w in next(lines).split()])
            current["frames"].append((int(words[2]), rows))
    return effects


# --- Drawing -------------------------------------------------------------------------

LEGEND = ("hue: R red, O orange, Y yellow, G green, C cyan, B blue, P purple/pink, "
          "W white/gray; digit: brightness 0-9; ' .' off; last column: mean row brightness")


def ascii_pixel(rgb):
    r, g, b = (c / 255 for c in rgb)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if v < 0.03:
        return " ."
    if s < 0.25:
        letter = "W"
    else:
        degrees = h * 360
        letter = ("R" if degrees < 15 or degrees >= 345 else "O" if degrees < 45 else
                  "Y" if degrees < 70 else "G" if degrees < 160 else "C" if degrees < 200 else
                  "B" if degrees < 260 else "P")
    return letter + str(min(9, int(v * 10)))


def frame_lines(rows, mode):
    if mode == "ascii":
        out = []
        for row in rows:
            mean = sum(max(p) for p in row) / (255 * len(row))
            out.append("".join(ascii_pixel(p) for p in row) + f" {mean:4.2f}")
        return out
    out = []
    for row in rows:
        cells = []
        for r, g, b in row:
            if max(r, g, b) < 8:
                cells.append("\x1b[38;2;60;60;60m· ")  # an LED that is off
            else:
                cells.append(f"\x1b[38;2;{r};{g};{b}m██")
        out.append("".join(cells) + "\x1b[0m")
    return out


def pick(frames, count):
    """count frames spread evenly over the whole time, the last one included."""
    if count >= len(frames):
        return frames
    step = (len(frames) - 1) / max(1, count - 1)
    return [frames[round(i * step)] for i in range(count)]


def show_static(effect, args):
    print(f"\n{effect['id']}  {effect['width']}x{effect['height']}, {effect['fps']} fps"
          f"  [{effect['params']}]")
    width = effect["width"] * 2 + (5 if args.mode == "ascii" else 0)
    per_line = max(1, (shutil.get_terminal_size((120, 24)).columns + 2) // (width + 2))
    chosen = pick(effect["frames"], args.frames)
    for start in range(0, len(chosen), per_line):
        group = chosen[start:start + per_line]
        print("  ".join(f"t={now} ms".ljust(width) for now, _ in group))
        blocks = [frame_lines(rows, args.mode) for _, rows in group]
        for line in zip(*blocks):
            print("  ".join(line))


def show_play(effect, args):
    print(f"\n{effect['id']}  [{effect['params']}]   Ctrl+C stops")
    height = effect["height"] + 1
    first = True
    while True:
        for now, rows in effect["frames"]:
            if not first:
                sys.stdout.write(f"\x1b[{height}A")
            first = False
            sys.stdout.write(f"t={now:6d} ms\n" + "\n".join(frame_lines(rows, args.mode)) + "\n")
            sys.stdout.flush()
            time.sleep(1 / effect["fps"])
        if not args.loop:
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("effects", nargs="*", help="id[:param=value,...]; default: all")
    parser.add_argument("--ms", type=int, default=3000, help="time to render (3000)")
    parser.add_argument("--fps", type=int, default=25, help="frames per second (25)")
    parser.add_argument("--size", default="15x10", help="matrix size (15x10)")
    parser.add_argument("--gamma", action="store_true", help="user colors with gamma correction")
    parser.add_argument("--mode", choices=("color", "ascii"),
                        help="color or ascii; default: color in a terminal, else ascii")
    parser.add_argument("--frames", type=int, default=6, help="frames to print (6)")
    parser.add_argument("--play", action="store_true", help="animate in place")
    parser.add_argument("--loop", action="store_true", help="with --play: repeat")
    parser.add_argument("--dump", help="read this dump instead of rendering")
    parser.add_argument("--keep", help="also save the rendered dump to this file")
    args = parser.parse_args()
    args.mode = args.mode or ("color" if sys.stdout.isatty() else "ascii")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if os.name == "nt":
        os.system("")  # lets the Windows console understand color codes

    if args.dump:
        effects = read_dump(args.dump)
    else:
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "frames.txt")
            render(args, path)
            effects = read_dump(path)
            if args.keep:
                shutil.copyfile(path, args.keep)
    if args.mode == "ascii":
        print(LEGEND)
    try:
        for effect in effects:
            (show_play if args.play else show_static)(effect, args)
    except KeyboardInterrupt:
        print("\x1b[0m")


if __name__ == "__main__":
    main()

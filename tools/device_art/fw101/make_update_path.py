"""Make updater/assets/update-path-1.0.1.js: the display of firmware 1.0.1 on the way to the
update mode (counter, both buttons to Settings, button 2 down to "FIRMWARE: UPDATE", hold button 2,
"FIRMWARE UPDATE / Waiting..."). The web updater plays it on step 1 of the firmware update.

Why 1.0.1 and not the firmware of this repository: most people who open the updater have a device
with 1.0.1 (sold devices), and they do not know the update path yet. When most devices have the new
firmware, make this animation from the headless simulator of this repository instead.

    python tools/device_art/fw101/make_update_path.py <folder with the 1.0.1 source>

The 1.0.1 source: tag v1.0.1 of the original firmware repository (it has main/, components/ and
sim/simulator.c). This script compiles that simulator with SDL.h and headless.c of this folder
(no window: virtual time, scripted buttons), runs flow.txt and writes the frames.
Needs a C compiler: zig from the .venv of this repository (python tools/setup.py simulator) or gcc.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OUT = os.path.join(ROOT, "updater", "assets", "update-path-1.0.1.js")
VIEWS = ["intro", "ask", "total", "kill_dead", "settings", "update", "stats", "fw_accept", "msg", "auto_off",
         "battery_low", "battery_critical"]
STEP_MS = 20          # frames closer than this are merged (the display runs at 100 frames/s)


def compiler():
    venv_python = os.path.join(ROOT, ".venv", "Scripts", "python.exe") if os.name == "nt" else \
        os.path.join(ROOT, ".venv", "bin", "python")
    if os.path.exists(venv_python):
        probe = subprocess.run([venv_python, "-m", "ziglang", "version"], capture_output=True)
        if probe.returncode == 0:
            return [venv_python, "-m", "ziglang", "cc"]
    for name in ("gcc", "clang", "cc"):
        if shutil.which(name):
            return [name]
    sys.exit("No C compiler: run python tools/setup.py simulator first")


def build(src, work):
    os.makedirs(os.path.join(work, "SDL2"))
    shutil.copy(os.path.join(HERE, "SDL.h"), os.path.join(work, "SDL2", "SDL.h"))
    m, c, sim = os.path.join(src, "main"), os.path.join(src, "components"), os.path.join(src, "sim")
    files = [os.path.join(HERE, "headless.c")]
    files += [os.path.join(sim, "board", f) for f in ("bsp_button.c", "bsp_oled.c", "bsp_buzzer.c", "board.c")]
    files += [os.path.join(sim, "mock", f) for f in ("ota.c", "nvs_flash.c")]
    files += [os.path.join(m, f) for f in ("app.c", "battery.c", "eeprom.c", "app_view.c", "gfx_fx.c")]
    files += [os.path.join(m, "views", v + "_view.c") for v in VIEWS]
    files += [os.path.join(m, "resources", f) for f in ("font.c", "font_glyph.c", "img.c")]
    files += [os.path.join(c, "gfx", "gfx.c"), os.path.join(c, "gui", "animation.c"), os.path.join(c, "gui", "queue.c")]
    inc = [work, sim, os.path.join(sim, "mock"), os.path.join(sim, "board"), m, os.path.join(m, "resources"),
           os.path.join(m, "views"), os.path.join(c, "gfx"), os.path.join(c, "gui"), os.path.join(c, "ssd1307"),
           os.path.join(c, "ble_ota")]
    exe = os.path.join(work, "sim101.exe" if os.name == "nt" else "sim101")
    cmd = compiler() + ["-std=gnu11", "-O1", "-w", "-DSIMULATOR=1", "-DGFX_SUPPORT_GLYPH_FONT=1",
                        '-DPROJECT_VER="1.0.1"'] + ["-I" + i for i in inc] + files + ["-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def frames(exe):
    with open(os.path.join(HERE, "flow.txt"), "rb") as script:
        out = subprocess.run([exe], stdin=script, capture_output=True, check=True).stdout.decode()
    raw = []
    for line in out.splitlines():
        if line.startswith("F "):
            _, t, press, rows = line.split()
            raw.append((int(t), press, rows.split(",")))
    # Merge frames closer than STEP_MS (keep a frame when the input changes)
    kept = []
    for t, press, rows in raw:
        if kept and t - kept[-1][0] < STEP_MS and press == kept[-1][1]:
            kept[-1] = (kept[-1][0], press, rows)
        else:
            kept.append((t, press, rows))
    result = []
    for i, (t, press, rows) in enumerate(kept):
        ms = (kept[i + 1][0] - t) if i + 1 < len(kept) else 1500
        if result and result[-1]["rows"] == rows and result[-1]["press"] == press:
            result[-1]["ms"] += ms
            continue
        result.append({"ms": ms, "press": press, "rows": rows})
    return result


def main():
    if len(sys.argv) != 2 or not os.path.isfile(os.path.join(sys.argv[1], "sim", "simulator.c")):
        sys.exit(__doc__)
    work = tempfile.mkdtemp()
    try:
        result = frames(build(os.path.abspath(sys.argv[1]), work))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    about = ("The display of firmware 1.0.1 on the way to the update mode, from its simulator "
             "(tools/device_art/fw101/make_update_path.py). Each frame: ms = how long it shows, press = the "
             "buttons at that time (- none, 1 or 2 = a short press, H2 = button 2 held, B = both), rows = 32 rows "
             "of 128 pixels as 32 hex digits, the left pixel is the high bit.")
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        # A script, not a .json file: the page loads it with a <script> tag (also when it is opened as a file)
        handle.write("/* Made by tools/device_art/fw101/make_update_path.py. Do not change it by hand. */\n")
        handle.write("window.STATTRAK_UPDATE_PATH = ")
        json.dump({"about": about, "firmware": "1.0.1", "frames": result}, handle, separators=(",", ":"))
        handle.write(";\n")
    print("Wrote %s: %d frames, %.1f s" % (OUT, len(result), sum(f["ms"] for f in result) / 1000))


if __name__ == "__main__":
    main()

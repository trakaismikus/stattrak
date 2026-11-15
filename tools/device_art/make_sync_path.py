"""Make updater/assets/sync-path.js: the display of the firmware of this repository on the way to
"Apps: SYNC" (kill counter, both buttons to the app menu, left button to SETTINGS, both buttons to
open it, right button down to "Apps: SYNC", hold the right button, "APP SYNC / Waiting...").
The web updater plays it on the Apps tab before "Connect".

    python tools/check.py --build-only            # the headless simulator
    python tools/device_art/make_sync_path.py

Run it again after a change to the kill counter, the app menu, the settings list or the App Sync
screen. tests/test_sync_path.py (firmware checks) fails when the file is not the same as the
firmware draws it now.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tests"))
from stattrak_sim import Device, SETTING_ROWS  # noqa: E402

OUT = os.path.join(ROOT, "updater", "assets", "sync-path.js")
STEP_MS = 20                       # the display runs at 100 frames/s; one frame each 20 ms is enough
KILLS = 7                          # kills on the counter before the recording starts
SHORT_MS = 90                      # a short press (the firmware sees it 180 ms after the release)
HOLD_MS = 900                      # a hold (long press: 700 ms)


def hex_rows(frame):
    rows = []
    for line in frame:
        bits = "".join("1" if c == "#" else "0" for c in line)
        rows.append("".join("%x" % int(bits[i:i + 4], 2) for i in range(0, 128, 4)))
    return rows


class Recorder:
    def __init__(self, dev):
        self.dev = dev
        self.frames = []            # [ms, press, rows]

    def wait(self, ms, press="-"):
        for _ in range(max(1, ms // STEP_MS)):
            self.dev.run(STEP_MS)
            rows = hex_rows(self.dev.frame())
            last = self.frames[-1] if self.frames else None
            if last and last[1] == press and last[2] == rows:
                last[0] += STEP_MS
            else:
                self.frames.append([STEP_MS, press, rows])

    def click(self, button, gap):
        self.dev.press(button)
        self.wait(SHORT_MS, str(button))
        self.dev.release(button)
        self.wait(gap)

    def click_both(self, gap):
        self.dev.press(1)
        self.dev.press(2)
        self.wait(SHORT_MS, "B")
        self.dev.release(1)
        self.dev.release(2)
        self.wait(gap)

    def hold(self, button, ms):
        self.dev.press(button)
        self.wait(ms, "H%d" % button)
        self.dev.release(button)


def record():
    with Device() as dev:
        dev.boot(settle_ms=2500)
        for _ in range(KILLS):
            dev.click(1, settle_ms=300)
        dev.run(2500)
        rec = Recorder(dev)
        rec.wait(1800)                                   # the kill counter
        rec.click_both(1100)                             # the app menu
        for _ in range(12):                              # left button: back to SETTINGS (the last app)
            if dev.state()["launcher_sel"] == "SETTINGS":
                break
            rec.click(1, 1000)
        else:
            raise SystemExit("SETTINGS not found in the app menu")
        rec.click_both(1200)                             # open Settings
        if dev.view != "SETTINGS":
            raise SystemExit("Settings did not open (view %s)" % dev.view)
        target = SETTING_ROWS.index("APP_SYNC")
        for _ in range(2 * len(SETTING_ROWS)):           # right button: down to "Apps: SYNC"
            if dev.state()["settings_row"] == target:
                break
            rec.click(2, 360)
        else:
            raise SystemExit('"Apps: SYNC" not found in Settings')
        rec.wait(500)
        rec.hold(2, HOLD_MS)                             # hold the right button
        rec.wait(3200)                                   # APP SYNC, Waiting...
        if dev.view != "APP_SYNC":
            raise SystemExit("App Sync did not open (view %s)" % dev.view)
    return [{"ms": ms, "press": press, "rows": rows} for ms, press, rows in rec.frames]


ABOUT = ("The display of the firmware of this repository on the way to Settings > Apps: SYNC, from the "
         "headless simulator (tools/device_art/make_sync_path.py). Each frame: ms = how long it shows, press = "
         "the buttons at that time (- none, 1 or 2 = a short press, H2 = button 2 held, B = both), rows = 32 "
         "rows of 128 pixels as 32 hex digits, the left pixel is the high bit.")


def write(frames, path=OUT):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        # A script, not a .json file: the page loads it with a <script> tag (also when opened as a file)
        handle.write("/* Made by tools/device_art/make_sync_path.py. Do not change it by hand. */\n")
        handle.write("window.STATTRAK_SYNC_PATH = ")
        json.dump({"about": ABOUT, "frames": frames}, handle, separators=(",", ":"))
        handle.write(";\n")


def main():
    frames = record()
    write(frames)
    print("Wrote %s: %d frames, %.1f s" % (OUT, len(frames), sum(f["ms"] for f in frames) / 1000))


if __name__ == "__main__":
    main()

"""Make updater/tests/sync-screens.json: the screens of Settings > "Apps: SYNC" from the firmware
(main/views/app_sync_view.c), taken from the headless simulator. The web updater test
(updater/tests/apps.test.js) checks that js/apps-pictures.js draws the same pixels.

Run it after a change to _draw() of app_sync_view.c or to main/views/hold_button.c:
    python tests/make_sync_screens.py
It needs the built simulator (python tools/check.py --build-only).
"""

import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tests"))
from stattrak_sim import Device, APP_CMD_BEGIN, APP_CMD_ASK, APP_CMD_DATA, APP_CMD_END  # noqa: E402

OUT = os.path.join(ROOT, "updater", "tests", "sync-screens.json")
BUTTON_W = 22                      # NO, YES, BACK, OPEN (hold_button.h HOLD_BUTTON_W)
SYNC_MORE_W = 40


def hex_rows(frame):
    rows = []
    for line in frame:
        bits = "".join("1" if c == "#" else "0" for c in line)
        rows.append("".join("%x" % int(bits[i:i + 4], 2) for i in range(0, 128, 4)))
    return rows


def fill_of(width, pixels):
    """The fill percent that the page uses for `pixels` inverted columns of a button of `width`"""
    for fill in range(101):
        if int(((width - 2) * fill + 50) // 100) == pixels:
            return fill
    raise ValueError(pixels)


class Maker:
    def __init__(self):
        self.screens = []
        self.dev = Device()

    def add(self, kind, arg=None, fill=None):
        entry = {"kind": kind}
        if arg is not None:
            entry["arg"] = arg
        if fill is not None:
            entry["fill"] = fill
        entry["rows"] = hex_rows(self.dev.frame())
        self.screens.append(entry)

    def wait_dots(self, dots):
        while (self.dev.state()["t"] >> 9) & 3 != dots:
            self.dev.run(16)

    def hold_until(self, button, x, width, pixels):
        """Hold a button until `pixels` columns of the button at x are filled. Returns the fill."""
        self.dev.press(button)
        for _ in range(200):
            self.dev.run(5)
            row = self.dev.frame()[22]          # The middle row inside the buttons
            inside = row[x + 1:x + width - 1]
            filled = len(inside) - len(inside.lstrip("#"))
            if filled >= pixels:
                return fill_of(width, filled)
        raise RuntimeError("the button did not fill")

    def begin_ask(self, package, name=None):
        app_id = struct.unpack_from("<I", package, 8)[0]
        assert self.dev.ble_command(APP_CMD_BEGIN, struct.pack("<IHI", app_id, len(package), zlib.crc32(package)))[0] == 0
        field = (name.encode("ascii").ljust(16, b"\0") if name else package[16:32])
        assert self.dev.ble_command(APP_CMD_ASK, field)[0] == 0
        self.dev.run(100)

    def send(self, package, until=None):
        chunks = [(APP_CMD_DATA, struct.pack("<H", o) + package[o:o + 14].ljust(14, b"\0"))
                  for o in range(0, len(package), 14)]
        if until is not None:
            chunks = chunks[:until]
        self.dev.ble_commands(chunks)

    def run(self):
        dev = self.dev
        package = open(os.path.join(ROOT, "updater", "apps", "reaction.stapp"), "rb").read()
        dev.boot()
        dev.open_app_sync()
        dev.run(600)
        for dots in (1, 3):
            self.wait_dots(dots)
            self.add("idle", dots)
        # BACK while it is held
        fill = self.hold_until(1, 4, BUTTON_W, 8)
        self.wait_dots(3)
        self.add("idle", 3, {"no": fill})
        dev.release(1)
        dev.run(300)

        # The question, then the hold of YES and of NO (released before the long press)
        self.begin_ask(package)
        self.add("question", "REACTION")
        fill = self.hold_until(2, 102, BUTTON_W, 9)
        self.add("question", "REACTION", {"no": 0, "yes": fill})
        dev.release(2)
        dev.run(300)
        fill = self.hold_until(1, 4, BUTTON_W, 5)
        self.add("question", "REACTION", {"no": fill, "yes": 0})
        dev.release(1)
        dev.run(300)
        dev.long_press(2)                                  # Yes

        # Receiving
        self.add("receiving", 0)
        self.send(package, until=(len(package) // 14) // 3 + 1)
        dev.run(50)
        self.add("receiving", self._percent(package))
        self.send_rest(package)
        assert dev.ble_command(APP_CMD_END)[0] == 0
        dev.run(100)
        self.add("installed", "REACTION")
        fill = self.hold_until(1, 4, SYNC_MORE_W, 19)
        self.add("installed", "REACTION", {"no": fill, "yes": 0})
        dev.release(1)
        dev.run(300)

        # A long name: the question in the small font
        self.begin_ask(package, "ABCDEFGHIJKLMN")
        self.add("question", "ABCDEFGHIJKLMN")
        dev.long_press(1)                                  # No
        dev.run(300)
        self.add("refused")
        fill = self.hold_until(1, 4, BUTTON_W, 10)
        self.add("refused", None, {"no": fill})
        dev.release(1)
        dev.run(300)

        # NOT INSTALLED: the transfer stopped (hold 1 while it receives)
        self.begin_ask(package)
        dev.long_press(2)
        self.send(package, until=3)
        dev.long_press(1)
        dev.run(300)
        self.add("notinstalled")
        dev.close()

    def _percent(self, package):
        sent = min(len(package), ((len(package) // 14) // 3 + 1) * 14)
        self._sent = sent
        return sent * 100 // len(package)

    def send_rest(self, package):
        chunks = [(APP_CMD_DATA, struct.pack("<H", o) + package[o:o + 14].ljust(14, b"\0"))
                  for o in range(self._sent, len(package), 14)]
        self.dev.ble_commands(chunks)


ABOUT = ("The display of Settings > Apps: SYNC on the firmware (main/views/app_sync_view.c), from the headless "
         "simulator (tests/make_sync_screens.py). Each row: 128 pixels as 32 hex digits, the left pixel is the "
         "high bit. A fill is the part of a hold of the button (0..100): no = button 1, yes = button 2. "
         "Make it again after a change to _draw() or to main/views/hold_button.c.")


def build():
    """The screens of the firmware now, as the dict of sync-screens.json"""
    maker = Maker()
    maker.run()
    return {"about": ABOUT, "screens": maker.screens}


def main():
    data = build()
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(data, handle, indent=1)
        handle.write("\n")
    print("Wrote %s: %d screens" % (OUT, len(data["screens"])))


if __name__ == "__main__":
    main()

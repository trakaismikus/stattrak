"""The first project of the Screen Designer: the 4 built-in apps (designs/stattrak-apps.json).

The design must show the same pixels as the firmware. The test opens each app on the simulated
device with the same values as the design (1337 total kills, 512 total deaths, 42 kills and
17 deaths in this game, battery type 1xRCR123A at 4.00 V) and compares all pixels.

When a test fails after a change of a view, change designs/stattrak-apps.json, then copy it into
updater/designer/default-design.js (the same JSON after "window.STATTRAK_DEFAULT_DESIGN = ").
"""

import json
import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
sys.path.insert(0, os.path.join(REPO_ROOT, "tests"))

import design  # noqa: E402
from helpers import BATTERY_RCR123A, SimTestCase  # noqa: E402
from stattrak_sim import APP_MENU_MAX  # noqa: E402

DESIGN_FILE = os.path.join(REPO_ROOT, "designs", "stattrak-apps.json")
DESIGNER_FILE = os.path.join(REPO_ROOT, "updater", "designer", "default-design.js")
PREFIX = "window.STATTRAK_DEFAULT_DESIGN = "

# Screen id in the design -> view of the firmware
VIEWS = {"bigCounter": "TOTAL", "splitCounter": "KILL_DEAD", "stats": "STATS", "settings": "SETTINGS"}
VALUES = {"total_kills": 1337, "total_deaths": 512, "game_kills": 42, "game_deaths": 17,
          "battery_type": BATTERY_RCR123A}


def load_design():
    with open(DESIGN_FILE, "r", encoding="utf-8") as handle:
        return json.load(handle)


class DefaultDesignFileTest(unittest.TestCase):

    def test_the_file_is_correct(self):
        clean, problems, warnings = design.normalize(load_design(), design.load_assets())
        self.assertEqual(problems, [])
        self.assertEqual(warnings, [])
        self.assertEqual([screen["id"] for screen in clean["screens"]], list(VIEWS))

    def test_the_designer_has_the_same_design(self):
        with open(DESIGNER_FILE, "r", encoding="utf-8") as handle:
            text = handle.read()
        start = text.index(PREFIX) + len(PREFIX)
        data = text[start:].strip()
        self.assertTrue(data.endswith(";"), "default-design.js must end with ';'")
        self.assertEqual(json.loads(data[:-1]), load_design())

    def test_notes_name_the_buttons_left_and_right(self):
        # The page and the docs say "left button" / "right button", not "button 1" / "button 2"
        text = json.dumps(load_design()).lower()
        self.assertNotIn("button 1", text)
        self.assertNotIn("button 2", text)


class DefaultDesignPixelsTest(SimTestCase):

    def open_view(self, view):
        """Open an app from the app menu, as a person does (both buttons, right button, both buttons)."""
        dev = self.dev
        dev.click_both()
        dev.run(300)
        for _ in range(APP_MENU_MAX):
            if dev.state()["launcher_sel"] == view:
                break
            dev.click(2)
        dev.click_both()
        dev.run(1500)
        self.assertEqual(dev.view, view)

    def test_each_screen_is_the_firmware_screen(self):
        dev = self.dev
        dev.boot()
        dev.run(500)
        dev.power_cut()
        dev.write_flash(VALUES)
        dev.boot()
        dev.run(3000)                                      # the start animation is over
        self.assertEqual(dev.view, "TOTAL")

        assets = design.load_assets()
        clean, problems, _ = design.normalize(load_design(), assets)
        self.assertEqual(problems, [])
        for screen in clean["screens"]:
            view = VIEWS[screen["id"]]
            if view != "TOTAL":
                self.open_view(view)
            want = dev.frame()
            got = design.draw_frame(screen, assets)
            wrong = [(x, y) for y in range(design.HEIGHT) for x in range(design.WIDTH) if got[y][x] != want[y][x]]
            self.assertEqual(wrong, [], "%s: these pixels are not as on the device (x, y). Device:\n%s\nDesign:\n%s"
                             % (screen["name"], "\n".join(want), "\n".join(got)))


if __name__ == "__main__":
    unittest.main()

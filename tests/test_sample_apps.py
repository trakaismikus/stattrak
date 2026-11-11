"""The sample apps in apps/ on the real firmware (the simulator).

Each app is installed like the web updater does it, opened from the app menu and played with the
buttons. It must run without an app error. WALLPAPER must look the same as the KILLS screen.
"""

import glob
import os
import struct
import sys
import unittest

from helpers import BATTERY_LIPO_1S
from stattrak_sim import APP_CMD_SETTING
from test_apps import AppTestCase, INSTALLED, KILLS, OK

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
import app  # noqa: E402


def compiled(name):
    return app.compile_file(os.path.join(REPO_ROOT, "apps", name, name + ".sts"))


def app_id(package):
    return struct.unpack_from("<I", package, 8)[0]


class SampleAppsTest(AppTestCase):

    def install_and_open(self, package):
        result = self.install(package)
        self.assertEqual(result["state"], INSTALLED, result)
        self.leave_sync()
        self.open_app(app_id(package))
        self.assertEqual(self.dev.state()["app_running"], app_id(package))

    def assert_no_app_error(self):
        state = self.dev.state()
        self.assertEqual(state["app_error"], 0, state["app_error_text"])

    def test_every_sample_app_compiles(self):
        names = sorted(os.path.basename(os.path.dirname(path))
                       for path in glob.glob(os.path.join(REPO_ROOT, "apps", "*", "*.sts")))
        self.assertIn("runner", names)
        for name in names:
            with self.subTest(app=name):
                package = compiled(name)["package"]
                self.assertEqual(app.check(package)[0], 0)

    def test_runner_plays_and_ends(self):
        package = compiled("runner")["package"]
        self.install_and_open(package)
        self.dev.click(1)                      # start
        for _ in range(10):                    # jump now and then; without good jumps it ends
            self.dev.run(700)
            self.dev.click(2)
        self.dev.run(15000)
        self.assert_no_app_error()
        # GAME OVER: the run ended (the obstacles came), and a new run starts with a press
        self.assertIn("#", "".join(self.dev.frame()[8:13]))
        self.dev.run(1000)
        self.dev.click(1)
        self.dev.run(2000)
        self.assert_no_app_error()

    def test_slots_spins_wins_and_plays_the_song(self):
        package = compiled("slots")["package"]
        self.install_and_open(package)
        self.dev.open_app_sync()
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", app_id(package), 0, 2)), (OK, bytes([2])))
        self.leave_sync()
        self.open_app(app_id(package))
        clicks = self.dev.state()["clicks"]
        self.dev.click(2)                      # LUCK = ALWAYS: this spin wins
        self.dev.run(5000)                     # reels stop, the song and the confetti
        self.assert_no_app_error()
        self.assertGreaterEqual(self.dev.state()["clicks"] - clicks, 1 + 3 + 8)   # spin, 3 stops, notes
        for _ in range(3):
            self.dev.click(1)
            self.dev.run(5000)
        self.assert_no_app_error()

    def kills_frame(self):
        self.open_app(KILLS)
        self.dev.run(1000)
        frame = self.dev.frame()
        self.assertGreater(sum(row.count("#") for row in frame), 200)   # the big digits are there
        return frame

    def wallpaper_frame(self, battery_bar):
        package = compiled("wallpaper")["package"]
        result = self.install(package)
        self.assertEqual(result["state"], INSTALLED, result)
        self.assertEqual(self.cmd(APP_CMD_SETTING, struct.pack("<IBB", app_id(package), 0, battery_bar))[0], OK)
        self.leave_sync()
        self.open_app(app_id(package))
        self.dev.run(1000)
        self.assert_no_app_error()
        return self.dev.frame()

    def test_wallpaper_looks_like_kills(self):
        self.dev.power_cut()
        self.dev.write_flash({"total_kills": 1337})
        self.dev.boot()
        self.assertEqual(self.wallpaper_frame(0), self.kills_frame())

    def test_wallpaper_looks_like_kills_with_the_battery_bar(self):
        self.dev.power_cut()
        self.dev.write_flash({"total_kills": 1337, "battery_type": BATTERY_LIPO_1S, "show_battery": 1})
        self.dev.boot()
        self.assertEqual(self.wallpaper_frame(1), self.kills_frame())

    def test_wallpaper_digits_change_with_the_buttons(self):
        package = compiled("wallpaper")["package"]
        self.install_and_open(package)
        before = self.dev.frame()
        self.dev.long_press(1)                 # edit: the first digit
        self.dev.long_press(2)                 # 0 -> 1
        self.dev.run(5000)                     # the edit stops, the number is kept
        self.assert_no_app_error()
        after = self.dev.frame()
        self.assertNotEqual(before, after)
        # Only the first digit (x 6 .. 29) changed
        self.assertEqual([row[30:] for row in before], [row[30:] for row in after])


if __name__ == "__main__":
    unittest.main()

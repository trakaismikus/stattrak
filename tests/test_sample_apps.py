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
        # The jump starts when the button goes down (down1 / down2), not after the release
        self.dev.run(1000)
        clicks = self.dev.state()["clicks"]
        standing = self.dev.frame()
        self.dev.press(2)
        self.dev.run(60)
        self.assertEqual(self.dev.state()["clicks"], clicks + 1)       # The jump sound
        self.assertNotEqual(self.dev.frame()[2:20], standing[2:20])    # The character goes up
        self.dev.release(2)
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
        self.dev.click(2)                      # not editing: a short press changes nothing
        self.dev.run(400)
        self.assertEqual(self.dev.frame(), before)
        self.dev.long_press(1, hold_ms=1800)   # hold: edit the first digit (its repeats do not move it)
        self.dev.click(2)                      # 0 -> 1
        self.dev.run(3500)                     # 3 s without a button: the edit stops, the number is kept
        self.assert_no_app_error()
        after = self.dev.frame()
        self.assertNotEqual(before, after)
        # Only the first digit (x 6 .. 29) changed
        self.assertEqual([row[30:] for row in before], [row[30:] for row in after])

        # Hold 2 = the next digit, hold 1 = the digit before; press 2 = + 1, press 1 = - 1 (0 -> 9)
        self.dev.long_press(2)                 # edit, first digit: 1
        self.dev.click(1)                      # 1 -> 0
        self.dev.click(1)                      # 0 -> 9
        self.dev.long_press(2)                 # second digit: 1
        self.dev.long_press(2)                 # third digit: 3
        self.dev.long_press(1)                 # back to the second digit
        self.dev.click(2)                      # 1 -> 2
        self.dev.long_press(1)                 # first digit
        self.dev.long_press(1)                 # around: the last digit, 7
        self.dev.click(2)                      # 7 -> 8
        self.dev.run(3500)
        self.dev.click_both()                  # close the app: its saved value is written
        self.dev.run(500)
        self.assert_no_app_error()
        app_id = struct.unpack_from("<I", package, 8)[0]
        self.assertEqual(self.dev.read_blobs()["v%08x" % app_id][:4], struct.pack("<i", 92338))   # number


class RunnerDesignTest(unittest.TestCase):
    """RUNNER is drawn from the owner's design (apps/runner/design.json, Screen Designer)."""

    def test_game_over_is_the_design(self):
        import design
        result = compiled("runner")
        names = {name: index for index, name in result["global_names"].items()}
        machine = app.Machine(result["package"])
        machine.run("open")
        # The state of the design: HI 305, score 25 (road not moved), the character dead in the air
        # (7 pixels high), the large cactus at x 19 and the cacti together at x 77
        state = dict(best=305, state=2, dist=256000, last_h=7, w1=19 + 256, t1=1, w2=77 + 256, t2=2, t3=0,
                     over_at=machine.now)
        for name, value in state.items():
            machine.globals[names[name]] = value
        assets = design.load_assets()
        drawing, _ = design.load_design(os.path.join(REPO_ROOT, "apps", "runner", "design.json"), assets)
        screen = [s for s in drawing["screens"] if s["id"] == "gameOver"][0]
        self.assertEqual(machine.frame(), design.draw_frame(screen, assets))


if __name__ == "__main__":
    unittest.main()
